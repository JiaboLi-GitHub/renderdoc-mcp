#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/capture.h"
#include "core/info.h"
#include "core/session.h"

namespace renderdoc::mcp::tools {

void registerCaptureTools(ToolRegistry& registry) {
    registry.registerTool({
        "capture_frame",
        "Launch an application with RenderDoc injected, capture a frame after a delay, "
        "and automatically open it for analysis. Returns capture info (API, total "
        "event/draw counts) and the .rdc file path.",
        {{"type", "object"},
         {"properties",
          {{"exePath", {{"type", "string"},
                        {"description", "Absolute path to the target executable"}}},
           {"workingDir", {{"type", "string"},
                           {"description", "Working directory for the target. "
                                           "Defaults to exePath's parent directory"}}},
           {"cmdLine", {{"type", "string"},
                        {"description", "Command line arguments for the target"}}},
           {"delayFrames", {{"type", "integer"},
                            {"description", "Number of frames to wait before capturing. "
                                            "Default: 100"}}},
           {"outputPath", {{"type", "string"},
                           {"description", "Path for the .rdc file. Default: auto-generated "
                                           "in temp directory"}}}}},
         {"required", {"exePath"}}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            core::CaptureRequest req;
            req.exePath = args.at("exePath").get<std::string>();
            req.workingDir = args.value("workingDir", "");
            req.cmdLine = args.value("cmdLine", "");
            req.delayFrames = args.value("delayFrames", 100);
            req.outputPath = args.value("outputPath", "");

            auto result = core::captureFrame(session, req);

            // Session is now open — return same format as open_capture + path
            auto info = core::getCaptureInfo(session);
            auto j = to_json(info);
            j["path"] = result.capturePath;
            return j;
        }
    });
}

} // namespace renderdoc::mcp::tools
