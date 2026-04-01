#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/usage.h"

namespace renderdoc::mcp::tools {

void registerUsageTools(ToolRegistry& registry) {

    registry.registerTool({
        "get_resource_usage",
        "Get the usage history of a resource across all events. "
        "Shows which events read, write, or bind the resource.",
        {{"type", "object"},
         {"properties", {
             {"resourceId", {{"type", "string"}, {"description", "Resource ID (e.g. ResourceId::123)"}}}
         }},
         {"required", nlohmann::json::array({"resourceId"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            auto rid = parseResourceId(args["resourceId"].get<std::string>());
            auto result = core::getResourceUsage(session, rid);
            return to_json(result);
        }
    });
}

} // namespace renderdoc::mcp::tools
