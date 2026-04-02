#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/pixel.h"

namespace renderdoc::mcp::tools {

void registerPixelTools(ToolRegistry& registry) {

    registry.registerTool({
        "pixel_history",
        "Query the modification history of a pixel up to the current or specified event. "
        "Returns which draws wrote to the pixel, shader output values (float/uint/int), "
        "post-blend values, depth, and pass/fail status. "
        "Note: history is bounded by the specified eventId.",
        {{"type", "object"},
         {"properties", {
             {"x",           {{"type", "integer"}, {"description", "Pixel X coordinate"}}},
             {"y",           {{"type", "integer"}, {"description", "Pixel Y coordinate"}}},
             {"targetIndex", {{"type", "integer"}, {"description", "Color render target index (0-7), default 0"}}},
             {"eventId",     {{"type", "integer"}, {"description", "Event ID to query up to (default: current event)"}}}
         }},
         {"required", nlohmann::json::array({"x", "y"})}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            uint32_t x = args["x"].get<uint32_t>();
            uint32_t y = args["y"].get<uint32_t>();
            uint32_t targetIndex = args.value("targetIndex", 0u);
            std::optional<uint32_t> eventId;
            if (args.contains("eventId"))
                eventId = args["eventId"].get<uint32_t>();
            auto result = core::pixelHistory(session, x, y, targetIndex, eventId);
            return to_json(result);
        }
    });

    registry.registerTool({
        "pick_pixel",
        "Read the color value of a single pixel at the current or specified event. "
        "Returns float, uint, and int representations.",
        {{"type", "object"},
         {"properties", {
             {"x",           {{"type", "integer"}, {"description", "Pixel X coordinate"}}},
             {"y",           {{"type", "integer"}, {"description", "Pixel Y coordinate"}}},
             {"targetIndex", {{"type", "integer"}, {"description", "Color render target index (0-7), default 0"}}},
             {"eventId",     {{"type", "integer"}, {"description", "Event ID (default: current)"}}}
         }},
         {"required", nlohmann::json::array({"x", "y"})}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            uint32_t x = args["x"].get<uint32_t>();
            uint32_t y = args["y"].get<uint32_t>();
            uint32_t targetIndex = args.value("targetIndex", 0u);
            std::optional<uint32_t> eventId;
            if (args.contains("eventId"))
                eventId = args["eventId"].get<uint32_t>();
            auto result = core::pickPixel(session, x, y, targetIndex, eventId);
            return to_json(result);
        }
    });
}

} // namespace renderdoc::mcp::tools
