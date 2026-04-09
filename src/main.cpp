#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "renderdoc_replay.h"
#include "mcp/mcp_server.h"

using namespace renderdoc::mcp;

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

// Prevent RenderDoc from capturing this process
REPLAY_PROGRAM_MARKER()

using json = nlohmann::json;

static void writeResponse(const json& response)
{
    if(response.is_null())
        return;

    // Write as single-line JSON followed by newline (MCP stdio transport spec)
    std::string line = response.dump(-1, ' ', false, json::error_handler_t::replace);
    std::cout << line << "\n";
    std::cout.flush();
    if(std::cout.fail())
        return;  // Output pipe broken
}

static void writeToStderr(const std::string& msg)
{
    std::cerr << "[renderdoc-mcp] " << msg << std::endl;
}

int main(int argc, char* argv[])
{
#ifdef _WIN32
    // Set stdin/stdout to binary mode to prevent CRLF translation
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    writeToStderr("Server starting...");

    McpServer server;

    std::string line;
    while(std::getline(std::cin, line) && !std::cout.fail())
    {
        // Skip empty lines
        if(line.empty() || (line.size() == 1 && line[0] == '\r'))
            continue;

        // Strip trailing \r if present (from CRLF line endings)
        if(!line.empty() && line.back() == '\r')
            line.pop_back();

        json msg;
        try
        {
            msg = json::parse(line);
        }
        catch(const json::parse_error& e)
        {
            json errorResp;
            errorResp["jsonrpc"] = "2.0";
            errorResp["id"] = nullptr;
            errorResp["error"]["code"] = -32700;
            errorResp["error"]["message"] = std::string("Parse error: ") + e.what();
            writeResponse(errorResp);
            continue;
        }

        json response;
        if(msg.is_array())
        {
            // JSON-RPC batch
            if(msg.empty())
            {
                json errorResp;
                errorResp["jsonrpc"] = "2.0";
                errorResp["id"] = nullptr;
                errorResp["error"]["code"] = -32600;
                errorResp["error"]["message"] = "Invalid Request: empty batch";
                writeResponse(errorResp);
                continue;
            }
            response = server.handleBatch(msg);
        }
        else if(msg.is_object())
        {
            response = server.handleMessage(msg);
        }
        else
        {
            json errorResp;
            errorResp["jsonrpc"] = "2.0";
            errorResp["id"] = nullptr;
            errorResp["error"]["code"] = -32600;
            errorResp["error"]["message"] = "Invalid Request: expected object or array";
            writeResponse(errorResp);
            continue;
        }

        writeResponse(response);
    }

    writeToStderr("Server shutting down...");
    server.shutdown();

    return 0;
}
