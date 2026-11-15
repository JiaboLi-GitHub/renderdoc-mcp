#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "renderdoc_replay.h"
#include "mcp/mcp_server.h"
#include "server_log.h"

using namespace renderdoc::mcp;

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <malloc.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

// Prevent RenderDoc from capturing this process
REPLAY_PROGRAM_MARKER()

using json = nlohmann::json;

namespace {

constexpr size_t kMaxLoggedChars = 2000;

std::string truncateForLog(const std::string& text)
{
    if(text.size() <= kMaxLoggedChars)
        return text;
    return text.substr(0, kMaxLoggedChars) + "... (" + std::to_string(text.size()) + " bytes)";
}

void writeResponse(const json& response)
{
    if(response.is_null())
        return;

    // Write as single-line JSON followed by newline (MCP stdio transport spec)
    std::string line = response.dump(-1, ' ', false, json::error_handler_t::replace);
    std::cout << line << "\n";
    std::cout.flush();
}

void writeToStderr(const std::string& msg)
{
    std::cerr << "[renderdoc-mcp] " << msg << std::endl;
    serverlog::write("INFO", msg);
}

json requestId(const json& msg)
{
    return msg.is_object() && msg.contains("id") ? msg["id"] : json(nullptr);
}

// e.g. "tools/call 'debug_pixel' (id 7)"
std::string describeRequest(const json& msg)
{
    if(msg.is_array())
        return "batch of " + std::to_string(msg.size()) + " messages";
    if(!msg.is_object())
        return "invalid message";

    std::string text = msg.contains("method") && msg["method"].is_string()
        ? msg["method"].get<std::string>() : std::string("<no method>");
    if(text == "tools/call" && msg.contains("params") && msg["params"].is_object())
    {
        const json& params = msg["params"];
        if(params.contains("name") && params["name"].is_string())
            text += " '" + params["name"].get<std::string>() + "'";
    }
    text += msg.contains("id") ? " (id " + msg["id"].dump() + ")" : " (notification)";
    return text;
}

bool isToolCall(const json& msg)
{
    return msg.is_object() && msg.contains("method") && msg["method"] == "tools/call";
}

json makeError(const json& id, int code, const std::string& message)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

json makeToolError(const json& id, const std::string& message)
{
    return {{"jsonrpc", "2.0"},
            {"id", id},
            {"result", {{"content", json::array({{{"type", "text"}, {"text", message}}})},
                        {"isError", true}}}};
}

json dispatch(McpServer& server, const json& msg)
{
    try
    {
        if(msg.is_array())
        {
            if(msg.empty())
                return makeError(nullptr, -32600, "Invalid Request: empty batch");
            return server.handleBatch(msg);
        }
        if(msg.is_object())
            return server.handleMessage(msg);
        return makeError(nullptr, -32600, "Invalid Request: expected object or array");
    }
    catch(const std::exception& e)
    {
        std::string text = "Internal error while handling " + describeRequest(msg) + ": " + e.what();
        serverlog::write("ERROR", text);
        return makeError(requestId(msg), -32603, text);
    }
    catch(...)
    {
        std::string text = "Internal error while handling " + describeRequest(msg) +
                           ": non-standard C++ exception";
        serverlog::write("ERROR", text);
        return makeError(requestId(msg), -32603, text);
    }
}

#ifdef _WIN32
struct GuardedCall
{
    McpServer* server;
    const json* msg;
    json response;
};

std::string g_nativeDescription;
std::string g_nativeDump;

void runGuardedCall(void* context)
{
    auto* call = static_cast<GuardedCall*>(context);
    call->response = dispatch(*call->server, *call->msg);
}

int captureNativeException(EXCEPTION_POINTERS* info)
{
    // C++ exceptions are handled by dispatch(); only native faults are recovered here.
    if(info->ExceptionRecord->ExceptionCode == 0xE06D7363)
        return EXCEPTION_CONTINUE_SEARCH;
    g_nativeDescription = serverlog::describeNativeException(info);
    g_nativeDump = serverlog::writeMinidump(info);
    return EXCEPTION_EXECUTE_HANDLER;
}

// Kept free of C++ objects because it uses SEH.
bool runWithCrashGuard(void (*fn)(void*), void* context)
{
    __try
    {
        fn(context);
        return true;
    }
    __except(captureNativeException(GetExceptionInformation()))
    {
        if(GetExceptionCode() == EXCEPTION_STACK_OVERFLOW)
            _resetstkoflw();
        return false;
    }
}
#endif

json dispatchGuarded(McpServer& server, const json& msg)
{
#ifdef _WIN32
    GuardedCall call{&server, &msg, nullptr};
    if(runWithCrashGuard(runGuardedCall, &call))
        return std::move(call.response);

    std::string text = "Native crash while handling " + describeRequest(msg) + ": " +
                       g_nativeDescription +
                       ". RenderDoc's replay state may be inconsistent; close and reopen the "
                       "capture before continuing.";
    if(!g_nativeDump.empty())
        text += " Minidump: " + g_nativeDump;
    serverlog::write("ERROR", text);

    if(isToolCall(msg))
        return makeToolError(requestId(msg), text);
    return makeError(requestId(msg), -32603, text);
#else
    return dispatch(server, msg);
#endif
}

std::string summarizeResponse(const json& response)
{
    if(response.is_array())
        return "batch of " + std::to_string(response.size()) + " responses";
    if(response.contains("error"))
    {
        const json& error = response["error"];
        return "error " + std::to_string(error.value("code", 0)) + ": " +
               truncateForLog(error.value("message", std::string()));
    }
    const json& result = response.value("result", json::object());
    if(result.is_object() && result.value("isError", false) && result.contains("content") &&
       result["content"].is_array() && !result["content"].empty())
        return "tool error: " +
               truncateForLog(result["content"][0].value("text", std::string()));
    return "ok";
}

std::string parseLogPath(int argc, char* argv[])
{
    std::string path;
#ifdef _WIN32
    char* env = nullptr;
    size_t len = 0;
    if(_dupenv_s(&env, &len, "RENDERDOC_MCP_LOG") == 0 && env)
    {
        path = env;
        std::free(env);
    }
#else
    if(const char* env = std::getenv("RENDERDOC_MCP_LOG"))
        path = env;
#endif

    for(int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if(arg == "--log-file" && i + 1 < argc)
            path = argv[++i];
        else if(arg.rfind("--log-file=", 0) == 0)
            path = arg.substr(11);
    }
    return path;
}

} // namespace

int main(int argc, char* argv[])
{
#ifdef _WIN32
    // Set stdin/stdout to binary mode to prevent CRLF translation
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    std::string logPath = parseLogPath(argc, argv);
    if(!logPath.empty())
    {
        if(serverlog::open(logPath))
            serverlog::installCrashHandlers();
        else
            std::cerr << "[renderdoc-mcp] Cannot open log file: " << logPath << std::endl;
    }

    writeToStderr("Server starting...");
    if(serverlog::enabled())
    {
        std::string cmdline;
        for(int i = 0; i < argc; ++i)
            cmdline += (i ? " " : "") + std::string(argv[i]);
        serverlog::write("INFO", "Command line: " + cmdline);
    }

    McpServer server;

    std::string line;
    while(std::getline(std::cin, line) && !std::cout.fail())
    {
        // Strip trailing \r if present (from CRLF line endings)
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        if(line.empty())
            continue;

        serverlog::write("RECV", truncateForLog(line));

        json msg;
        try
        {
            msg = json::parse(line);
        }
        catch(const json::parse_error& e)
        {
            std::string text = std::string("Parse error: ") + e.what();
            serverlog::write("ERROR", text);
            writeResponse(makeError(nullptr, -32700, text));
            continue;
        }

        auto start = std::chrono::steady_clock::now();
        json response = dispatchGuarded(server, msg);
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        serverlog::reassertCrashHandlers();

        if(serverlog::enabled() && !response.is_null())
        {
            serverlog::write("SEND", describeRequest(msg) + " -> " + summarizeResponse(response) +
                                         " (" + std::to_string(elapsedMs) + " ms)");
        }
        writeResponse(response);
    }

    if(std::cout.fail())
        writeToStderr("Server shutting down: stdout write failed (client closed the pipe)");
    else if(std::cin.eof())
        writeToStderr("Server shutting down: stdin closed by client");
    else
        writeToStderr("Server shutting down: stdin read error");
    server.shutdown();
    serverlog::write("INFO", "Server exited cleanly");

    return 0;
}
