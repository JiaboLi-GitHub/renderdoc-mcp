#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"

namespace renderdoc::mcp::tools {

void registerSessionTools(ToolRegistry& registry) {
    registry.registerTool({
        "open_capture",
        "Open a RenderDoc capture file (.rdc) for analysis. Returns the graphics API type "
        "and total event/draw counts. Closes any previously opened capture.",
        {{"type", "object"},
         {"properties", {{"path", {{"type", "string"},
                                    {"description", "Absolute path to the .rdc capture file"}}}}},
         {"required", {"path"}}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            auto info = session.open(args["path"].get<std::string>());
            return to_json(info);
        }
    });
}

} // namespace renderdoc::mcp::tools
