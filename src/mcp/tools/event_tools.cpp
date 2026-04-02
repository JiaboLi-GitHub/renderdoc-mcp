#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/events.h"

namespace renderdoc::mcp::tools {

void registerEventTools(ToolRegistry& registry) {
    registry.registerTool({
        "list_events",
        "List all draw calls and actions in the currently opened capture.",
        {{"type", "object"},
         {"properties", {{"filter", {{"type", "string"},
                                      {"description", "Optional case-insensitive filter keyword"}}}}}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            auto filter = args.value("filter", std::string());
            auto events = core::listEvents(session, filter);
            return to_json_array(events);
        }
    });

    registry.registerTool({
        "goto_event",
        "Navigate to a specific event by its event ID.",
        {{"type", "object"},
         {"properties", {{"eventId", {{"type", "integer"}, {"description", "Event ID to navigate to"}}}}},
         {"required", {"eventId"}}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            auto info = core::gotoEvent(session, args["eventId"].get<uint32_t>());
            return to_json(info);
        }
    });

    registry.registerTool({
        "list_draws",
        "List draw calls with vertex/index counts, instance counts, and flags.",
        {{"type", "object"},
         {"properties", {
             {"filter", {{"type", "string"}, {"description", "Filter by name keyword"}}},
             {"limit", {{"type", "integer"}, {"description", "Max results (default 1000)"}}}
         }}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            auto filter = args.value("filter", std::string());
            auto limit = args.value("limit", 1000u);
            auto draws = core::listDraws(session, filter, limit);
            nlohmann::json result;
            result["draws"] = to_json_array(draws);
            result["count"] = draws.size();
            return result;
        }
    });

    registry.registerTool({
        "get_draw_info",
        "Get detailed information about a specific draw call.",
        {{"type", "object"},
         {"properties", {{"eventId", {{"type", "integer"}, {"description", "Event ID of the draw call"}}}}},
         {"required", {"eventId"}}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            auto info = core::getDrawInfo(session, args["eventId"].get<uint32_t>());
            return to_json(info);
        }
    });
}

} // namespace renderdoc::mcp::tools
