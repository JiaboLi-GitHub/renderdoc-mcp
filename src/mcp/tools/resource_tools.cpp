#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/pass_analysis.h"
#include "core/resources.h"

namespace renderdoc::mcp::tools {

void registerResourceTools(ToolRegistry& registry) {
    // list_resources
    registry.registerTool({
        "list_resources",
        "List all GPU resources (textures, buffers, shaders, etc.) in the capture with type and size info",
        {{"type", "object"},
         {"properties", {
             {"type", {{"type", "string"}, {"description", "Filter by resource type keyword (e.g. Texture, Buffer, Shader)"}}},
             {"name", {{"type", "string"}, {"description", "Filter by name keyword (case-insensitive)"}}}
         }}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            auto typeFilter = args.value("type", std::string());
            auto nameFilter = args.value("name", std::string());
            auto resources  = core::listResources(session, typeFilter, nameFilter);
            nlohmann::json result;
            result["resources"] = to_json_array(resources);
            result["count"]     = resources.size();
            return result;
        }
    });

    // get_resource_info
    registry.registerTool({
        "get_resource_info",
        "Get detailed information about a specific GPU resource by its ID",
        {{"type", "object"},
         {"properties", {
             {"resourceId", {{"type", "string"}, {"description", "Resource ID string (e.g. ResourceId::123)"}}}
         }},
         {"required", {"resourceId"}}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            auto idStr = args["resourceId"].get<std::string>();
            auto id    = parseResourceId(idStr);
            auto info  = core::getResourceDetails(session, id);
            return to_json(info);
        }
    });

    // list_passes
    registry.registerTool({
        "list_passes",
        "List all render passes in the capture. Returns marker-based passes when available, otherwise synthetic passes grouped by render target changes.",
        {{"type", "object"},
         {"properties", nlohmann::json::object()}},
        [](mcp::ToolContext& ctx, const nlohmann::json& /*args*/) -> nlohmann::json {
            auto& session = ctx.session;
            auto passes = core::enumeratePassRanges(session);
            nlohmann::json result;
            result["passes"] = to_json_array(passes);
            result["count"]  = passes.size();
            return result;
        }
    });

    // get_pass_info
    registry.registerTool({
        "get_pass_info",
        "Get details about a specific render pass including its draw calls",
        {{"type", "object"},
         {"properties", {
             {"eventId", {{"type", "integer"}, {"description", "Event ID of the pass marker"}}}
         }},
         {"required", {"eventId"}}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            auto eventId = args["eventId"].get<uint32_t>();
            auto pass    = core::getPassInfo(session, eventId);
            return to_json(pass);
        }
    });
}

} // namespace renderdoc::mcp::tools
