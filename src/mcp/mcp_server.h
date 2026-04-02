#pragma once

#include <nlohmann/json.hpp>
#include "mcp/tool_registry.h"
#include <memory>

namespace renderdoc::core { class Session; }
namespace renderdoc::core { class DiffSession; }

namespace renderdoc::mcp {

class McpServer
{
public:
    // Default constructor: creates own session + registers all tools (requires renderdoc at link time)
    McpServer();
    // Injection constructor: uses external session, diffSession & registry (no renderdoc dependency)
    McpServer(core::Session& session, core::DiffSession& diffSession, ToolRegistry& registry);
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

    std::unique_ptr<core::Session> m_ownedSession;        // owned, only set by default ctor
    core::Session* m_session = nullptr;                    // always valid (points to owned or injected)
    std::unique_ptr<core::DiffSession> m_ownedDiffSession; // owned, only set by default ctor
    core::DiffSession* m_diffSession = nullptr;            // always valid (points to owned or injected)
    std::unique_ptr<ToolRegistry> m_ownedRegistry;        // owned, only set by default ctor
    ToolRegistry* m_registry = nullptr;                    // always valid (points to owned or injected)
    bool m_initialized = false;
};

} // namespace renderdoc::mcp
