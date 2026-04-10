#include "core/capture.h"
#include "core/errors.h"
#include "core/session.h"

#include <renderdoc_replay.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#endif

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
#ifdef _WIN32
    localtime_s(&tm, &timeT);
#else
    localtime_r(&timeT, &tm);
#endif

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
    opts.hookIntoChildren = true;
    opts.refAllResources = true;
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
            // Match filenames that start with exeName or contain exeName preceded by
            // a separator, to avoid overly broad substring matches.
            auto pos = name.find(exeName);
            if (pos == std::string::npos) continue;
            if (pos != 0 && name[pos - 1] != '_' && name[pos - 1] != '-') continue;
            auto ftime = entry.last_write_time();
            if (found.empty() || ftime > newestTime) {
                found = entry.path().string();
                newestTime = ftime;
            }
        }
    }
    return found;
}

fs::path currentExecutablePath() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');

    while (true) {
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
        if (length == 0)
            throw CoreError(CoreError::Code::InternalError,
                            "Failed to locate the renderdoc-mcp executable");

        if (length < buffer.size() - 1) {
            buffer.resize(length);
            return fs::path(buffer);
        }

        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        throw CoreError(CoreError::Code::InternalError,
                        "Failed to locate the renderdoc-mcp executable");
    return fs::weakly_canonical(fs::path(buffer.c_str()));
#elif defined(__linux__)
    std::vector<char> buffer(PATH_MAX);
    while (true) {
        ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0)
            throw CoreError(CoreError::Code::InternalError,
                            "Failed to locate the renderdoc-mcp executable");

        if (static_cast<size_t>(length) < buffer.size())
            return fs::path(std::string(buffer.data(), static_cast<size_t>(length)));

        buffer.resize(buffer.size() * 2);
    }
#else
    return fs::current_path();
#endif
}

fs::path renderDocRuntimeDir() {
    return currentExecutablePath().parent_path();
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

    EnvironmentModification implicitLayerPath;
    implicitLayerPath.mod = EnvMod::Set;
    implicitLayerPath.sep = EnvSep::NoSep;
    implicitLayerPath.name = "VK_IMPLICIT_LAYER_PATH";
    const std::string runtimeDir = renderDocRuntimeDir().string();
    implicitLayerPath.value = runtimeDir.c_str();
    envMods.push_back(implicitLayerPath);

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
    // NOTE: The 2-second sleep and ident guessing below are heuristics required
    // because the RenderDoc API does not provide a reliable synchronization
    // mechanism for post-injection readiness. This is known tech debt.
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
            // Extended retry for multi-process apps (e.g. Chrome GPU process)
            for (int attempt = 0; attempt < 15; ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                // Try wider ident range for child processes
                for (uint32_t offset = 0; offset <= 10; ++offset) {
                    uint32_t tryIdent = execResult.ident + offset;
                    ctrl = RENDERDOC_CreateTargetControl(
                        rdcstr(), tryIdent, rdcstr("renderdoc-mcp"), true);
                    if (ctrl) {
                        targetIdent = tryIdent;
                        break;
                    }
                    if (offset > 0 && execResult.ident > offset) {
                        tryIdent = execResult.ident - offset;
                        ctrl = RENDERDOC_CreateTargetControl(
                            rdcstr(), tryIdent, rdcstr("renderdoc-mcp"), true);
                        if (ctrl) {
                            targetIdent = tryIdent;
                            break;
                        }
                    }
                }
                if (ctrl) break;
            }
        }
    }

    if (!ctrl)
        throw CoreError(CoreError::Code::InternalError,
                        "Failed to connect to target process");

    // RAII guard for ctrl->Shutdown() — single cleanup path, no manual Shutdown()
    struct CtrlGuard {
        ITargetControl* c;
        ~CtrlGuard() { if (c) c->Shutdown(); }
    };

    std::string foundCapture;
    uint32_t pid = 0;
    {
        CtrlGuard guard{ctrl};

        pid = ctrl->GetPID();

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
        std::string captureMsgPath; // path from NewCapture message (preferred)
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);

        while (std::chrono::steady_clock::now() < deadline) {
            if (!ctrl->Connected())
                break;

            TargetControlMessage msg = ctrl->ReceiveMessage(nullptr);

            if (msg.type == TargetControlMessageType::NewCapture) {
                captured = true;
                captureMsgPath = std::string(msg.newCapture.path.c_str());
                break;
            }
            if (msg.type == TargetControlMessageType::Disconnected)
                break;

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // 9. Find the capture file on disk.
        // Prefer the path from the NewCapture message (avoids filesystem race).
        // Fall back to scanning directories if the message path is unavailable.
        if (!captureMsgPath.empty() && fs::exists(captureMsgPath)) {
            foundCapture = captureMsgPath;
        } else {
            auto exeName = fs::path(req.exePath).stem().string();
            std::vector<fs::path> searchDirs = {
                fs::path(captureTemplate).parent_path(),
                fs::temp_directory_path() / "RenderDoc",
            };
            foundCapture = findNewestCapture(exeName, searchDirs);
        }

        if (foundCapture.empty())
            throw CoreError(CoreError::Code::InternalError,
                            captured ? "Capture completed but file not found on disk"
                                     : "Capture timed out and no capture file found");

        if (foundCapture != outputPath)
            fs::copy_file(foundCapture, outputPath, fs::copy_options::overwrite_existing);

    } // 10. guard destructor calls ctrl->Shutdown()

    // 11. Auto-open the capture
    session.open(outputPath);

    return CaptureResult{outputPath, pid};
}

} // namespace renderdoc::core
