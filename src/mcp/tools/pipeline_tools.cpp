#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/pipeline.h"

namespace renderdoc::mcp::tools {

void registerPipelineTools(ToolRegistry& registry) {

    // ── get_pipeline_state ────────────────────────────────────────────────────
    registry.registerTool({
        "get_pipeline_state",
        "Get the graphics pipeline state at the current or specified event. "
        "Returns bound shaders, render targets, viewports, and pipeline configuration.",
        {{"type", "object"},
         {"properties", {
             {"eventId", {{"type", "integer"}, {"description", "Event ID to inspect (uses current if omitted)"}}}
         }}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            std::optional<uint32_t> eventId;
            if (args.contains("eventId"))
                eventId = args["eventId"].get<uint32_t>();

            auto state = core::getPipelineState(session, eventId);
            return to_json(state);
        }
    });

    // ── get_bindings ──────────────────────────────────────────────────────────
    registry.registerTool({
        "get_bindings",
        "Get descriptor/resource bindings for all shader stages at the current or specified event. "
        "Shows constant buffers, textures, UAVs, and samplers from shader reflection.",
        {{"type", "object"},
         {"properties", {
             {"eventId", {{"type", "integer"}, {"description", "Event ID (uses current if omitted)"}}}
         }}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            std::optional<uint32_t> eventId;
            if (args.contains("eventId"))
                eventId = args["eventId"].get<uint32_t>();

            auto bindings = core::getBindings(session, eventId);

            nlohmann::json result;
            result["api"] = graphicsApiToString(session.status().api);
            result["eventId"] = eventId.has_value() ? *eventId : session.currentEventId();

            nlohmann::json stages = nlohmann::json::object();
            for (const auto& [stage, sb] : bindings) {
                stages[shaderStageToString(stage)] = to_json(sb);
            }
            result["stages"] = stages;
            return result;
        }
    });

}

} // namespace renderdoc::mcp::tools
