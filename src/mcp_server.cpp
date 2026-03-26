#include "mcp_server.h"
#include <stdexcept>

using json = nlohmann::json;

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

// ── Tool definitions ────────────────────────────────────────────────────────

json McpServer::getToolDefinitions()
{
    json tools = json::array();

    // 1. open_capture
    {
        json tool;
        tool["name"] = "open_capture";
        tool["description"] = "Open a RenderDoc capture file (.rdc) for analysis. Returns the graphics API type and total event count. Closes any previously opened capture.";
        tool["inputSchema"] = {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "Absolute path to the .rdc capture file"}}}
            }},
            {"required", json::array({"path"})}
        };
        tools.push_back(tool);
    }

    // 2. list_events
    {
        json tool;
        tool["name"] = "list_events";
        tool["description"] = "List all draw calls and actions in the currently opened capture. Returns event IDs, names, and action flags.";
        tool["inputSchema"] = {
            {"type", "object"},
            {"properties", {
                {"filter", {{"type", "string"}, {"description", "Optional case-insensitive filter keyword to match event names"}}}
            }}
        };
        tools.push_back(tool);
    }

    // 3. goto_event
    {
        json tool;
        tool["name"] = "goto_event";
        tool["description"] = "Navigate to a specific event by its event ID. This sets the replay position so that subsequent pipeline state queries and render target exports reflect this event.";
        tool["inputSchema"] = {
            {"type", "object"},
            {"properties", {
                {"eventId", {{"type", "integer"}, {"description", "The event ID to navigate to"}}}
            }},
            {"required", json::array({"eventId"})}
        };
        tools.push_back(tool);
    }

    // 4. get_pipeline_state
    {
        json tool;
        tool["name"] = "get_pipeline_state";
        tool["description"] = "Get the graphics pipeline state at the current event. Returns bound shaders (vertex/pixel/fragment), render targets, viewports, and other pipeline configuration. Call goto_event first to select which event to inspect.";
        tool["inputSchema"] = {
            {"type", "object"},
            {"properties", json::object()}
        };
        tools.push_back(tool);
    }

    // 5. export_render_target
    {
        json tool;
        tool["name"] = "export_render_target";
        tool["description"] = "Export the current event's render target as a PNG file. The file is saved to an auto-generated path in the capture's directory. Call goto_event first to select which event to export.";
        tool["inputSchema"] = {
            {"type", "object"},
            {"properties", {
                {"index", {{"type", "integer"}, {"description", "Render target index (0-7), defaults to 0"}, {"default", 0}}}
            }}
        };
        tools.push_back(tool);
    }

    return tools;
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
    result["tools"] = getToolDefinitions();
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

    json toolResult = callTool(toolName, arguments);
    return makeResponse(id, toolResult);
}

// ── Tool dispatch ───────────────────────────────────────────────────────────

json McpServer::callTool(const std::string& name, const json& arguments)
{
    try
    {
        if(name == "open_capture")
        {
            if(!arguments.contains("path") || !arguments["path"].is_string())
                return makeToolResult("Missing required parameter: path", true);
            auto result = m_wrapper.openCapture(arguments["path"].get<std::string>());
            return makeToolResult(result);
        }
        else if(name == "list_events")
        {
            std::string filter = arguments.value("filter", "");
            auto result = m_wrapper.listEvents(filter);
            return makeToolResult(result);
        }
        else if(name == "goto_event")
        {
            if(!arguments.contains("eventId") || !arguments["eventId"].is_number_integer())
                return makeToolResult("Missing required parameter: eventId (integer)", true);
            auto result = m_wrapper.gotoEvent(arguments["eventId"].get<uint32_t>());
            return makeToolResult(result);
        }
        else if(name == "get_pipeline_state")
        {
            auto result = m_wrapper.getPipelineState();
            return makeToolResult(result);
        }
        else if(name == "export_render_target")
        {
            int index = arguments.value("index", 0);
            auto result = m_wrapper.exportRenderTarget(index);
            return makeToolResult(result);
        }
        else
        {
            return makeToolResult("Unknown tool: " + name, true);
        }
    }
    catch(const std::exception& e)
    {
        return makeToolResult(std::string(e.what()), true);
    }
}
