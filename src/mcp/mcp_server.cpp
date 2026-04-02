#include "mcp/mcp_server.h"
#include "mcp/tool_registry.h"
#include "core/session.h"
#include "core/diff_session.h"
#include "core/errors.h"
#include <stdexcept>

using json = nlohmann::json;

namespace renderdoc::mcp {

// ── Injection constructor ──────────────────────────────────────────────────

McpServer::McpServer(core::Session& session, core::DiffSession& diffSession, ToolRegistry& registry)
    : m_session(&session)
    , m_diffSession(&diffSession)
    , m_registry(&registry)
    , m_initialized(false)
{
}

McpServer::~McpServer() = default;

void McpServer::shutdown()
{
    if(m_session)
        m_session->close();
    if(m_diffSession)
        m_diffSession->close();
}

// ── JSON-RPC helpers ────────────────────────────────────────────────────────

json McpServer::makeResponse(const json& id, const json& result)
{
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = id;
    resp["result"] = result;
    return resp;
}

json McpServer::makeError(const json& id, int code, const std::string& message)
{
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = id;
    resp["error"]["code"] = code;
    resp["error"]["message"] = message;
    return resp;
}

json McpServer::makeToolResult(const json& data, bool isError)
{
    json result;
    json content;
    content["type"] = "text";

    if(data.is_string())
        content["text"] = data.get<std::string>();
    else
        content["text"] = data.dump();

    result["content"] = json::array({content});
    if(isError)
        result["isError"] = true;
    return result;
}

// ── Message dispatch ────────────────────────────────────────────────────────

json McpServer::handleMessage(const json& msg)
{
    // Check for valid JSON-RPC 2.0
    if(!msg.contains("jsonrpc") || msg["jsonrpc"] != "2.0")
        return makeError(msg.value("id", json(nullptr)), -32600, "Invalid Request: missing jsonrpc 2.0");

    std::string method = msg.value("method", "");
    bool isNotification = !msg.contains("id");
    json id = msg.value("id", json(nullptr));

    // Route methods
    if(method == "initialize")
        return handleInitialize(msg);
    else if(method == "notifications/initialized")
    {
        m_initialized = true;
        return nullptr;  // No response for notifications
    }
    else if(method == "tools/list")
        return handleToolsList(msg);
    else if(method == "tools/call")
        return handleToolsCall(msg);
    else if(isNotification)
        return nullptr;  // Unknown notifications are silently ignored
    else
        return makeError(id, -32601, "Method not found: " + method);
}

json McpServer::handleBatch(const json& arr)
{
    // Check for initialize in batch (forbidden by MCP spec)
    for(const auto& msg : arr)
    {
        if(msg.is_object() && msg.value("method", "") == "initialize")
            return makeError(nullptr, -32600, "Invalid Request: initialize must not appear in a JSON-RPC batch");
    }

    json responses = json::array();
    for(const auto& msg : arr)
    {
        if(!msg.is_object())
        {
            responses.push_back(makeError(nullptr, -32600, "Invalid Request: batch element is not an object"));
            continue;
        }
        json resp = handleMessage(msg);
        if(!resp.is_null())
            responses.push_back(resp);
    }

    // If all were notifications, return nothing
    if(responses.empty())
        return nullptr;

    return responses;
}

// ── MCP method handlers ─────────────────────────────────────────────────────

json McpServer::handleInitialize(const json& msg)
{
    json id = msg.value("id", json(nullptr));

    json result;
    result["protocolVersion"] = "2025-03-26";
    result["capabilities"]["tools"] = json::object();
    result["serverInfo"]["name"] = "renderdoc-mcp";
    result["serverInfo"]["version"] = "1.0.0";

    return makeResponse(id, result);
}

json McpServer::handleToolsList(const json& msg)
{
    json id = msg.value("id", json(nullptr));
    json result;
    result["tools"] = m_registry->getToolDefinitions();
    return makeResponse(id, result);
}

json McpServer::handleToolsCall(const json& msg)
{
    json id = msg.value("id", json(nullptr));
    json params = msg.value("params", json::object());

    std::string toolName = params.value("name", "");
    json arguments = params.value("arguments", json::object());

    if(toolName.empty())
        return makeError(id, -32602, "Invalid params: missing tool name");

    try
    {
        ToolContext ctx{*m_session, *m_diffSession};
        json rawResult = m_registry->callTool(toolName, ctx, arguments);
        return makeResponse(id, makeToolResult(rawResult));
    }
    catch(const InvalidParamsError& e)
    {
        // Protocol-level error: unknown tool, missing required, type mismatch, bad enum
        return makeError(id, -32602, std::string("Invalid params: ") + e.what());
    }
    catch(const core::CoreError& e)
    {
        // Core-level error: no capture open, invalid event id, etc.
        return makeResponse(id, makeToolResult(std::string(e.what()), true));
    }
    catch(const std::exception& e)
    {
        // Tool-level error: renderdoc API failure, etc.
        return makeResponse(id, makeToolResult(std::string(e.what()), true));
    }
}

} // namespace renderdoc::mcp
