#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"

namespace renderdoc::mcp::tools {

void registerSessionTools(ToolRegistry& registry) {
    registry.registerTool({
        "open_capture",
        "Open a RenderDoc capture file (.rdc) for analysis. Returns the graphics API type "
        "and total event/draw counts. Closes any previously opened capture.",
        {{"type", "object"},
         {"properties", {{"path", {{"type", "string"},
                                    {"description", "Absolute path to the .rdc capture file"}}}}},
         {"required", {"path"}}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            auto info = session.open(args["path"].get<std::string>());
            return to_json(info);
        }
    });

    registry.registerTool({
        "close_capture",
        "Close the currently opened capture and release resources.",
        {{"type", "object"}, {"properties", nlohmann::json::object()}},
        [](mcp::ToolContext& ctx, const nlohmann::json&) -> nlohmann::json {
            ctx.session.close();
            return {{"status", "closed"}};
        }
    });

    registry.registerTool({
        "session_status",
        "Query whether a capture is currently open. Returns session state including "
        "capture path, API type, current event ID, and total events.",
        {{"type", "object"}, {"properties", nlohmann::json::object()}},
        [](mcp::ToolContext& ctx, const nlohmann::json&) -> nlohmann::json {
            return to_json(ctx.session.status());
        }
    });
}

} // namespace renderdoc::mcp::tools
