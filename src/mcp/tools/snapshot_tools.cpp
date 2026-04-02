#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/snapshot.h"

namespace renderdoc::mcp::tools {

void registerSnapshotTools(ToolRegistry& registry) {

    registry.registerTool({
        "export_snapshot",
        "Export complete draw call state: pipeline, shaders, render targets, and depth. "
        "Creates a directory with manifest.json indexing all exported files.",
        {{"type", "object"},
         {"properties", {
             {"eventId",   {{"type", "integer"}, {"description", "Draw call event ID"}}},
             {"outputDir", {{"type", "string"}, {"description", "Output directory path"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "outputDir"})}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            uint32_t eventId = args["eventId"].get<uint32_t>();
            auto outputDir = args["outputDir"].get<std::string>();
            // Inject pipeline serializer from MCP layer into core
            auto pipeSerializer = [](const core::PipelineState& ps) -> std::string {
                return to_json(ps).dump(2);
            };
            auto result = core::exportSnapshot(session, eventId, outputDir, pipeSerializer);
            return to_json(result);
        }
    });
}

} // namespace renderdoc::mcp::tools
