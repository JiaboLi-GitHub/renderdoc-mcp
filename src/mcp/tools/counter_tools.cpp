#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/counters.h"
#include "core/session.h"

namespace renderdoc::mcp::tools {

void registerCounterTools(ToolRegistry& registry) {
    // --- list_counters ---
    registry.registerTool({
        "list_counters",
        "Enumerate all GPU performance counters available for the current capture. "
        "Returns counter IDs, names, categories, result types, and units. "
        "Use this to discover what counters can be fetched with fetch_counters.",
        {{"type", "object"},
         {"properties", nlohmann::json::object()}},
        [](mcp::ToolContext& ctx, const nlohmann::json& /*args*/) -> nlohmann::json {
            auto counters = core::listCounters(ctx.session);
            nlohmann::json result;
            result["counters"] = to_json_array(counters);
            result["count"] = counters.size();
            return result;
        }
    });

    // --- fetch_counters ---
    registry.registerTool({
        "fetch_counters",
        "Fetch GPU performance counter values for draw calls. "
        "Can filter by counter name (substring match) and/or event ID. "
        "Returns per-event counter values useful for identifying GPU bottlenecks "
        "(fill rate, bandwidth, ALU utilization, GPU duration, etc.).",
        {{"type", "object"},
         {"properties", {
             {"counterNames", {{"type", "array"},
                               {"items", {{"type", "string"}}},
                               {"description", "Filter counters by name (case-insensitive substring match). "
                                               "Empty or omitted = fetch all counters."}}},
             {"eventId", {{"type", "integer"},
                          {"description", "If specified, only return results for this event ID."}}}
         }}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            std::vector<std::string> counterNames;
            if (args.contains("counterNames")) {
                for (const auto& n : args["counterNames"])
                    counterNames.push_back(n.get<std::string>());
            }
            std::optional<uint32_t> eventId;
            if (args.contains("eventId"))
                eventId = args["eventId"].get<uint32_t>();

            auto result = core::fetchCounters(ctx.session, counterNames, eventId);
            return to_json(result);
        }
    });

    // --- get_counter_summary ---
    registry.registerTool({
        "get_counter_summary",
        "Get a summary of GPU performance showing the top-N most expensive draw calls "
        "by GPU duration. Useful for quickly identifying performance bottlenecks.",
        {{"type", "object"},
         {"properties", {
             {"limit", {{"type", "integer"},
                        {"description", "Maximum number of top draws to return (default: 10)."},
                        {"default", 10}}}
         }}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            uint32_t limit = args.value("limit", 10u);

            // Fetch GPU duration counter
            std::vector<std::string> durationFilter = {"GPU Duration"};
            auto result = core::fetchCounters(ctx.session, durationFilter);

            if (result.rows.empty()) {
                // Try alternative name patterns
                durationFilter = {"Duration"};
                result = core::fetchCounters(ctx.session, durationFilter);
            }

            // Sort by value descending
            std::sort(result.rows.begin(), result.rows.end(),
                      [](const core::CounterSample& a, const core::CounterSample& b) {
                          return a.value > b.value;
                      });

            // Truncate to limit
            if (result.rows.size() > limit)
                result.rows.resize(limit);

            nlohmann::json j;
            j["topDraws"] = to_json_array(result.rows);
            j["count"] = result.rows.size();
            j["totalCounters"] = result.totalCounters;
            return j;
        }
    });
}

} // namespace renderdoc::mcp::tools
