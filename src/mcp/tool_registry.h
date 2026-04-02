#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace renderdoc::core { class Session; }
namespace renderdoc::core { class DiffSession; }

namespace renderdoc::mcp {

// Protocol-level parameter error -- McpServer converts to JSON-RPC -32602
struct InvalidParamsError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ToolContext {
    core::Session& session;
    core::DiffSession& diffSession;
};

struct ToolDef {
    std::string name;
    std::string description;
    nlohmann::json inputSchema;
    // Returns raw JSON business data. Throws std::runtime_error for tool-level errors.
    std::function<nlohmann::json(ToolContext&, const nlohmann::json&)> handler;
};

class ToolRegistry {
public:
    void registerTool(ToolDef def);
    nlohmann::json getToolDefinitions() const;
    // Flow: lookup → validateArgs → call handler
    // InvalidParamsError for validation failures, std::runtime_error for tool errors
    nlohmann::json callTool(const std::string& name,
                            ToolContext& ctx,
                            const nlohmann::json& args);
    bool hasTool(const std::string& name) const;

private:
    void validateArgs(const ToolDef& tool, const nlohmann::json& args) const;

    std::vector<ToolDef> m_tools;
    std::unordered_map<std::string, size_t> m_toolIndex;
};

} // namespace renderdoc::mcp
