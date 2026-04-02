#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/diff_session.h"
#include "core/diff.h"

namespace renderdoc::mcp::tools {

using json = nlohmann::json;

void registerDiffTools(ToolRegistry& registry) {

    registry.registerTool({
        "diff_open",
        "Open two RenderDoc captures for side-by-side diffing. "
        "Returns capture info for both captures.",
        {{"type", "object"},
         {"properties", {
             {"captureA", {{"type", "string"}, {"description", "Path to capture A"}}},
             {"captureB", {{"type", "string"}, {"description", "Path to capture B"}}}
         }},
         {"required", json::array({"captureA", "captureB"})}},
        [](mcp::ToolContext& ctx, const json& args) -> json {
            auto pathA = args["captureA"].get<std::string>();
            auto pathB = args["captureB"].get<std::string>();
            auto result = ctx.diffSession.open(pathA, pathB);
            return to_json(result);
        }
    });

    registry.registerTool({
        "diff_close",
        "Close the current diff session and release both captures.",
        {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}},
        [](mcp::ToolContext& ctx, const json&) -> json {
            ctx.diffSession.close();
            return {{"success", true}};
        }
    });

    registry.registerTool({
        "diff_summary",
        "Get a high-level summary diff between the two open captures. "
        "Returns per-category counts (draws, resources, passes) with deltas.",
        {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}},
        [](mcp::ToolContext& ctx, const json&) -> json {
            return to_json(core::diffSummary(ctx.diffSession));
        }
    });

    registry.registerTool({
        "diff_draws",
        "Diff draw calls between the two open captures. "
        "Returns aligned rows with added/deleted/modified/unchanged counts.",
        {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}},
        [](mcp::ToolContext& ctx, const json&) -> json {
            return to_json(core::diffDraws(ctx.diffSession));
        }
    });

    registry.registerTool({
        "diff_resources",
        "Diff resources (textures, buffers) between the two open captures. "
        "Returns aligned rows with status and type information.",
        {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}},
        [](mcp::ToolContext& ctx, const json&) -> json {
            return to_json(core::diffResources(ctx.diffSession));
        }
    });

    registry.registerTool({
        "diff_stats",
        "Diff per-pass statistics between the two open captures. "
        "Returns pass rows with draw/triangle/dispatch deltas.",
        {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}},
        [](mcp::ToolContext& ctx, const json&) -> json {
            return to_json(core::diffStats(ctx.diffSession));
        }
    });

    registry.registerTool({
        "diff_pipeline",
        "Diff pipeline state at a named marker between the two captures. "
        "Returns field-level differences for the matched draw.",
        {{"type", "object"},
         {"properties", {
             {"marker", {{"type", "string"}, {"description", "Marker path to locate the draw in both captures"}}}
         }},
         {"required", json::array({"marker"})}},
        [](mcp::ToolContext& ctx, const json& args) -> json {
            auto marker = args["marker"].get<std::string>();
            return to_json(core::diffPipeline(ctx.diffSession, marker));
        }
    });

    registry.registerTool({
        "diff_framebuffer",
        "Compare framebuffer output between the two captures at specific event IDs. "
        "Returns pixel-level diff statistics and optionally writes a diff image.",
        {{"type", "object"},
         {"properties", {
             {"eidA",       {{"type", "integer"}, {"description", "Event ID in capture A (default: 0 = last)"}}},
             {"eidB",       {{"type", "integer"}, {"description", "Event ID in capture B (default: 0 = last)"}}},
             {"target",     {{"type", "integer"}, {"description", "Render target index (default: 0)"}}},
             {"threshold",  {{"type", "number"},  {"description", "Max diff ratio to pass (default: 0.0)"}}},
             {"diffOutput", {{"type", "string"},  {"description", "Path to write diff visualization PNG"}}}
         }},
         {"required", json::array()}},
        [](mcp::ToolContext& ctx, const json& args) -> json {
            uint32_t eidA      = args.value("eidA",      0u);
            uint32_t eidB      = args.value("eidB",      0u);
            int      target    = args.value("target",    0);
            double   threshold = args.value("threshold", 0.0);
            auto     diffOut   = args.value("diffOutput", std::string(""));
            return to_json(core::diffFramebuffer(ctx.diffSession, eidA, eidB, target, threshold, diffOut));
        }
    });
}

} // namespace renderdoc::mcp::tools
