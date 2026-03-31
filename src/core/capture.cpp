#include "core/capture.h"
#include "core/errors.h"
#include "core/session.h"

#include <renderdoc_replay.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

namespace renderdoc::core {

namespace fs = std::filesystem;

namespace {

std::string generateOutputPath(const std::string& exePath) {
    auto tempDir = fs::temp_directory_path() / "renderdoc-mcp";
    fs::create_directories(tempDir);

    auto exeName = fs::path(exePath).stem().string();

    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &timeT);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);

    return (tempDir / (exeName + "_" + buf + ".rdc")).string();
}

CaptureOptions makeDefaultOptions() {
    CaptureOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.allowVSync = true;
    opts.allowFullscreen = true;
    opts.apiValidation = false;
    opts.captureCallstacks = false;
    opts.captureCallstacksOnlyActions = false;
    opts.delayForDebugger = 0;
    opts.verifyBufferAccess = false;
    opts.hookIntoChildren = false;
    opts.refAllResources = false;
    opts.captureAllCmdLists = false;
    opts.debugOutputMute = true;
    opts.softMemoryLimit = 0;
    return opts;
}

} // anonymous namespace

CaptureResult captureFrame(Session& session, const CaptureRequest& req) {
    // 1. Validate inputs
    if (!fs::exists(req.exePath))
        throw CoreError(CoreError::Code::FileNotFound,
                        "Target executable not found: " + req.exePath);

    std::string workingDir = req.workingDir;
    if (workingDir.empty())
        workingDir = fs::path(req.exePath).parent_path().string();

    if (!fs::is_directory(workingDir))
        throw CoreError(CoreError::Code::InternalError,
                        "Working directory not found: " + workingDir);

    // 2. Generate output path
    std::string outputPath = req.outputPath;
    if (outputPath.empty())
        outputPath = generateOutputPath(req.exePath);

    // Ensure output directory exists
    auto outputDir = fs::path(outputPath).parent_path();
    if (!outputDir.empty())
        fs::create_directories(outputDir);

    // 3. Configure capture options
    auto opts = makeDefaultOptions();

    // Capture file template: RenderDoc uses this as prefix, appends _frameN.rdc
    // We strip the .rdc extension for the template
    std::string captureTemplate = outputPath;
    if (captureTemplate.size() > 4 &&
        captureTemplate.substr(captureTemplate.size() - 4) == ".rdc")
        captureTemplate = captureTemplate.substr(0, captureTemplate.size() - 4);

    // 4. Ensure replay is initialized (needed for ExecuteAndInject)
    session.ensureReplayInitialized();

    // 5. Launch and inject
    rdcarray<EnvironmentModification> envMods;
    ExecuteResult execResult = RENDERDOC_ExecuteAndInject(
        rdcstr(req.exePath.c_str()),
        rdcstr(workingDir.c_str()),
        rdcstr(req.cmdLine.c_str()),
        envMods,
        rdcstr(captureTemplate.c_str()),
        opts,
        false // don't wait for exit
    );

    if (execResult.result.code != ResultCode::Succeeded)
        throw CoreError(CoreError::Code::InternalError,
                        "Failed to launch and inject: " +
                        std::string(execResult.result.Message().c_str()));

    // 6. Connect target control
    ITargetControl* ctrl = RENDERDOC_CreateTargetControl(
        rdcstr(), execResult.ident, rdcstr("renderdoc-mcp"), true);

    if (!ctrl)
        throw CoreError(CoreError::Code::InternalError,
                        "Failed to connect to target process");

    // RAII guard for ctrl->Shutdown()
    struct CtrlGuard {
        ITargetControl* c;
        ~CtrlGuard() { if (c) c->Shutdown(); }
    } guard{ctrl};

    uint32_t pid = ctrl->GetPID();

    // 7. Queue capture at frame N
    ctrl->QueueCapture(req.delayFrames, 1);

    // 8. Poll for NewCapture message
    uint32_t captureId = 0;
    bool captured = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);

    while (std::chrono::steady_clock::now() < deadline) {
        if (!ctrl->Connected())
            throw CoreError(CoreError::Code::InternalError,
                            "Target process exited before capture completed");

        TargetControlMessage msg = ctrl->ReceiveMessage(nullptr);

        if (msg.type == TargetControlMessageType::NewCapture) {
            captureId = msg.newCapture.captureId;
            captured = true;
            break;
        }

        if (msg.type == TargetControlMessageType::Disconnected)
            throw CoreError(CoreError::Code::InternalError,
                            "Target process disconnected before capture completed");

        // Noop or other messages — continue polling
        if (msg.type == TargetControlMessageType::Noop)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!captured)
        throw CoreError(CoreError::Code::InternalError,
                        "Capture timed out after 60 seconds");

    // 9. Copy capture file to outputPath
    ctrl->CopyCapture(captureId, rdcstr(outputPath.c_str()));

    // Wait for CaptureCopied confirmation
    auto copyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    bool copied = false;
    while (std::chrono::steady_clock::now() < copyDeadline) {
        TargetControlMessage msg = ctrl->ReceiveMessage(nullptr);
        if (msg.type == TargetControlMessageType::CaptureCopied) {
            copied = true;
            break;
        }
        if (msg.type == TargetControlMessageType::Disconnected)
            break; // File may already be copied locally
        if (msg.type == TargetControlMessageType::Noop)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Verify the file exists regardless of copy confirmation
    if (!fs::exists(outputPath))
        throw CoreError(CoreError::Code::InternalError,
                        "Failed to copy capture file to: " + outputPath);

    // 10. Cleanup — shutdown target control before opening capture
    guard.c = nullptr; // prevent double-shutdown
    ctrl->Shutdown();

    // 11. Auto-open the capture
    session.open(outputPath);

    return CaptureResult{outputPath, pid};
}

} // namespace renderdoc::core
