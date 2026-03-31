#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/info.h"

namespace renderdoc::mcp::tools {

void registerInfoTools(ToolRegistry& registry) {
    registry.registerTool({
        "get_capture_info",
        "Get metadata about the currently opened capture file including API, GPU, "
        "resolution, total event/draw counts, and driver info.",
        {{"type", "object"},
         {"properties", nlohmann::json::object()}},
        [](core::Session& session, const nlohmann::json& /*args*/) -> nlohmann::json {
            auto info = core::getCaptureInfo(session);
            return to_json(info);
        }
    });

    registry.registerTool({
        "get_stats",
        "Get performance statistics: per-pass breakdown, top draws by triangle count, "
        "largest resources.",
        {{"type", "object"},
         {"properties", nlohmann::json::object()}},
        [](core::Session& session, const nlohmann::json& /*args*/) -> nlohmann::json {
            auto stats = core::getStats(session);
            return to_json(stats);
        }
    });

    registry.registerTool({
        "get_log",
        "Get debug/validation messages from the capture.",
        {{"type", "object"},
         {"properties", {
             {"level",   {{"type", "string"},
                          {"enum", nlohmann::json::array({"HIGH", "MEDIUM", "LOW", "INFO"})},
                          {"description", "Minimum severity level"}}},
             {"eventId", {{"type", "integer"},
                          {"description", "Filter by event ID"}}}
         }}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            std::string level = args.value("level", std::string());

            std::optional<uint32_t> eventId;
            if (args.contains("eventId") && args["eventId"].is_number_integer())
                eventId = args["eventId"].get<uint32_t>();

            auto msgs = core::getLog(session, level, eventId);

            nlohmann::json result;
            result["messages"] = to_json_array(msgs);
            result["count"]    = msgs.size();
            return result;
        }
    });
}

} // namespace renderdoc::mcp::tools
