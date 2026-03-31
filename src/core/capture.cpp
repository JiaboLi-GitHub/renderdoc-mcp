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

// Search for the newest .rdc file matching exeName in the given directories.
std::string findNewestCapture(const std::string& exeName,
                              const std::vector<fs::path>& searchDirs) {
    std::string found;
    fs::file_time_type newestTime{};
    for (const auto& dir : searchDirs) {
        if (!fs::exists(dir)) continue;
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() != ".rdc") continue;
            auto name = entry.path().filename().string();
            if (name.find(exeName) == std::string::npos) continue;
            auto ftime = entry.last_write_time();
            if (found.empty() || ftime > newestTime) {
                found = entry.path().string();
                newestTime = ftime;
            }
        }
    }
    return found;
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

    auto outputDir = fs::path(outputPath).parent_path();
    if (!outputDir.empty())
        fs::create_directories(outputDir);

    // 3. Configure capture options
    auto opts = makeDefaultOptions();

    // Capture file template (RenderDoc appends _frameN.rdc)
    std::string captureTemplate = outputPath;
    if (captureTemplate.size() > 4 &&
        captureTemplate.substr(captureTemplate.size() - 4) == ".rdc")
        captureTemplate = captureTemplate.substr(0, captureTemplate.size() - 4);

    // 4. Ensure replay is initialized
    session.ensureReplayInitialized();

    // 5. Launch and inject
    rdcarray<EnvironmentModification> envMods;
    EnvironmentModification enableVk;
    enableVk.mod = EnvMod::Set;
    enableVk.sep = EnvSep::NoSep;
    enableVk.name = "ENABLE_VULKAN_RENDERDOC_CAPTURE";
    enableVk.value = "1";
    envMods.push_back(enableVk);

    ExecuteResult execResult = RENDERDOC_ExecuteAndInject(
        rdcstr(req.exePath.c_str()),
        rdcstr(workingDir.c_str()),
        rdcstr(req.cmdLine.c_str()),
        envMods,
        rdcstr(captureTemplate.c_str()),
        opts,
        false
    );

    if (execResult.result.code != ResultCode::Succeeded)
        throw CoreError(CoreError::Code::InternalError,
                        "Failed to launch and inject: " +
                        std::string(execResult.result.Message().c_str()));

    // 6. Connect target control.
    // Wait for the target process to initialize after injection.
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    // The actual target ident may differ from what ExecuteAndInject returns
    // (typically ident+1). Try nearby values.
    ITargetControl* ctrl = nullptr;
    uint32_t targetIdent = execResult.ident;
    {
        uint32_t candidates[] = {
            execResult.ident + 1,
            execResult.ident + 2,
            execResult.ident,
            execResult.ident > 0 ? execResult.ident - 1 : 0,
        };
        for (uint32_t candidate : candidates) {
            if (candidate == 0) continue;
            ctrl = RENDERDOC_CreateTargetControl(
                rdcstr(), candidate, rdcstr("renderdoc-mcp"), true);
            if (ctrl) {
                targetIdent = candidate;
                break;
            }
        }
        if (!ctrl) {
            for (int attempt = 0; attempt < 5; ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                ctrl = RENDERDOC_CreateTargetControl(
                    rdcstr(), execResult.ident, rdcstr("renderdoc-mcp"), true);
                if (ctrl) {
                    targetIdent = execResult.ident;
                    break;
                }
            }
        }
    }

    if (!ctrl)
        throw CoreError(CoreError::Code::InternalError,
                        "Failed to connect to target process");

    // RAII guard for ctrl->Shutdown()
    struct CtrlGuard {
        ITargetControl* c;
        ~CtrlGuard() { if (c) c->Shutdown(); }
    } guard{ctrl};

    uint32_t pid = ctrl->GetPID();

    // 7. Wait for delayFrames, then trigger capture.
    {
        uint32_t waitMs = req.delayFrames * 16; // ~16ms per frame at 60fps
        if (waitMs < 2000) waitMs = 2000;       // minimum 2s for API init
        auto waitUntil = std::chrono::steady_clock::now() +
                         std::chrono::milliseconds(waitMs);
        while (std::chrono::steady_clock::now() < waitUntil) {
            if (!ctrl->Connected())
                throw CoreError(CoreError::Code::InternalError,
                                "Target process exited during wait");
            TargetControlMessage msg = ctrl->ReceiveMessage(nullptr);
            if (msg.type == TargetControlMessageType::Disconnected)
                throw CoreError(CoreError::Code::InternalError,
                                "Target process disconnected during wait");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    ctrl->TriggerCapture(1);

    // 8. Poll for NewCapture message
    bool captured = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);

    while (std::chrono::steady_clock::now() < deadline) {
        if (!ctrl->Connected())
            break;

        TargetControlMessage msg = ctrl->ReceiveMessage(nullptr);

        if (msg.type == TargetControlMessageType::NewCapture) {
            captured = true;
            break;
        }
        if (msg.type == TargetControlMessageType::Disconnected)
            break;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 9. Find the capture file on disk.
    // RenderDoc saves captures to its default temp directory (%TEMP%/RenderDoc/)
    // or the template path we provided.
    auto exeName = fs::path(req.exePath).stem().string();
    std::vector<fs::path> searchDirs = {
        fs::path(captureTemplate).parent_path(),
        fs::temp_directory_path() / "RenderDoc",
    };
    std::string foundCapture = findNewestCapture(exeName, searchDirs);

    if (foundCapture.empty())
        throw CoreError(CoreError::Code::InternalError,
                        captured ? "Capture completed but file not found on disk"
                                 : "Capture timed out and no capture file found");

    if (foundCapture != outputPath)
        fs::copy_file(foundCapture, outputPath, fs::copy_options::overwrite_existing);

    // 10. Cleanup
    guard.c = nullptr;
    ctrl->Shutdown();

    // 11. Auto-open the capture
    session.open(outputPath);

    return CaptureResult{outputPath, pid};
}

} // namespace renderdoc::core
