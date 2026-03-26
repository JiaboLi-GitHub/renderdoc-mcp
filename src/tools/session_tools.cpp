#include "tools.h"
#include "../tool_registry.h"
#include "../renderdoc_wrapper.h"

using json = nlohmann::json;

void registerSessionTools(ToolRegistry& registry)
{
    // open_capture
    registry.registerTool({
        "open_capture",
        "Open a RenderDoc capture file (.rdc) for analysis. Returns the graphics API type and total event count. Closes any previously opened capture.",
        {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "Absolute path to the .rdc capture file"}}}
            }},
            {"required", json::array({"path"})}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            return w.openCapture(args["path"].get<std::string>());
        }
    });
}
