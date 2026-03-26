#pragma once

#include <nlohmann/json.hpp>
#include "tool_registry.h"
#include <memory>

class RenderdocWrapper;

class McpServer
{
public:
    // Default constructor: creates own wrapper + registers all tools (requires renderdoc at link time)
    McpServer();
    // Injection constructor: uses external registry & wrapper (no renderdoc dependency)
    McpServer(ToolRegistry& registry, RenderdocWrapper& wrapper);
    ~McpServer();

    // Process a single JSON-RPC message. Returns response JSON, or nullptr for notifications.
    nlohmann::json handleMessage(const nlohmann::json& msg);

    // Process a JSON-RPC batch (array). Returns response array.
    nlohmann::json handleBatch(const nlohmann::json& arr);

    void shutdown();

private:
    // MCP method handlers
    nlohmann::json handleInitialize(const nlohmann::json& msg);
    nlohmann::json handleToolsList(const nlohmann::json& msg);
    nlohmann::json handleToolsCall(const nlohmann::json& msg);

    // JSON-RPC response helpers
    static nlohmann::json makeResponse(const nlohmann::json& id, const nlohmann::json& result);
    static nlohmann::json makeError(const nlohmann::json& id, int code, const std::string& message);
    static nlohmann::json makeToolResult(const nlohmann::json& data, bool isError = false);

    std::unique_ptr<RenderdocWrapper> m_ownedWrapper;  // owned, only set by default ctor
    RenderdocWrapper* m_wrapper = nullptr;              // always valid (points to owned or injected)
    ToolRegistry m_ownedRegistry;                       // owned, only populated by default ctor
    ToolRegistry* m_registry = nullptr;                 // always valid (points to owned or injected)
    bool m_initialized = false;
};
