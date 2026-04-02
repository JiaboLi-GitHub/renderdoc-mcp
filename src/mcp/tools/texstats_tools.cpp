#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/texstats.h"

namespace renderdoc::mcp::tools {

void registerTexStatsTools(ToolRegistry& registry) {

    registry.registerTool({
        "get_texture_stats",
        "Get min/max pixel values and optionally a 256-bucket histogram for a texture. "
        "Returns typed values (float/uint/int). "
        "Useful for detecting NaN values, all-black textures, or unexpected value ranges.",
        {{"type", "object"},
         {"properties", {
             {"resourceId", {{"type", "string"},
                             {"description", "Texture resource ID (e.g. ResourceId::123)"}}},
             {"mip",        {{"type", "integer"}, {"description", "Mip level, default 0"}}},
             {"slice",      {{"type", "integer"}, {"description", "Array slice, default 0"}}},
             {"histogram",  {{"type", "boolean"}, {"description", "Include 256-bucket RGBA histogram, default false"}}},
             {"eventId",    {{"type", "integer"}, {"description", "Event ID for texture state (default: current)"}}}
         }},
         {"required", nlohmann::json::array({"resourceId"})}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            std::string idStr = args["resourceId"].get<std::string>();
            core::ResourceId id = parseResourceId(idStr);
            uint32_t mip   = args.value("mip", 0u);
            uint32_t slice = args.value("slice", 0u);
            bool hist      = args.value("histogram", false);
            std::optional<uint32_t> eventId;
            if (args.contains("eventId"))
                eventId = args["eventId"].get<uint32_t>();
            auto result = core::getTextureStats(session, id, mip, slice, hist, eventId);
            return to_json(result);
        }
    });
}

} // namespace renderdoc::mcp::tools
