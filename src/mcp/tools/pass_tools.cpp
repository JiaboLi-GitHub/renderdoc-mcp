#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/pass_analysis.h"

namespace renderdoc::mcp::tools {

void registerPassTools(ToolRegistry& registry) {
    registry.registerTool({
        "get_pass_attachments",
        "Query color and depth attachments for a render pass (format, dimensions, resource IDs)",
        {{"type", "object"},
         {"properties", {
             {"eventId", {{"type", "integer"}, {"description", "Event ID within the pass (marker or draw)"}}}
         }},
         {"required", {"eventId"}}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto eventId = args["eventId"].get<uint32_t>();
            auto pa = core::getPassAttachments(ctx.session, eventId);
            return to_json(pa);
        }
    });

    registry.registerTool({
        "get_pass_statistics",
        "Return per-pass aggregated statistics: draw/dispatch counts, triangles, RT dimensions",
        {{"type", "object"},
         {"properties", nlohmann::json::object()}},
        [](mcp::ToolContext& ctx, const nlohmann::json& /*args*/) -> nlohmann::json {
            auto stats = core::getPassStatistics(ctx.session);
            nlohmann::json result;
            result["passes"] = to_json_array(stats);
            result["count"] = stats.size();
            return result;
        }
    });

    registry.registerTool({
        "get_pass_deps",
        "Build inter-pass resource dependency DAG showing which passes feed data to others",
        {{"type", "object"},
         {"properties", nlohmann::json::object()}},
        [](mcp::ToolContext& ctx, const nlohmann::json& /*args*/) -> nlohmann::json {
            auto graph = core::getPassDependencies(ctx.session);
            return to_json(graph);
        }
    });

    registry.registerTool({
        "find_unused_targets",
        "Detect render targets written but never consumed by visible output (optimization hints)",
        {{"type", "object"},
         {"properties", nlohmann::json::object()}},
        [](mcp::ToolContext& ctx, const nlohmann::json& /*args*/) -> nlohmann::json {
            auto result = core::findUnusedTargets(ctx.session);
            return to_json(result);
        }
    });
}

} // namespace renderdoc::mcp::tools
