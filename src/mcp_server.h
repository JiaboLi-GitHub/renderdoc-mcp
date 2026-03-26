#pragma once

#include <nlohmann/json.hpp>
#include "renderdoc_wrapper.h"
#include "tool_registry.h"

class McpServer
{
public:
    McpServer();
    ~McpServer() = default;

    // Process a single JSON-RPC message. Returns response JSON, or nullptr for notifications.
    nlohmann::json handleMessage(const nlohmann::json& msg);

    // Process a JSON-RPC batch (array). Returns response array.
    nlohmann::json handleBatch(const nlohmann::json& arr);

    void shutdown() { m_wrapper.shutdown(); }

private:
    // MCP method handlers
    nlohmann::json handleInitialize(const nlohmann::json& msg);
    nlohmann::json handleToolsList(const nlohmann::json& msg);
    nlohmann::json handleToolsCall(const nlohmann::json& msg);

    // JSON-RPC response helpers
    static nlohmann::json makeResponse(const nlohmann::json& id, const nlohmann::json& result);
    static nlohmann::json makeError(const nlohmann::json& id, int code, const std::string& message);
    static nlohmann::json makeToolResult(const nlohmann::json& data, bool isError = false);

    RenderdocWrapper m_wrapper;
    ToolRegistry m_registry;
    bool m_initialized = false;
};
