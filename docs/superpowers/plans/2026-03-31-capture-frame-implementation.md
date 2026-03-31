# capture_frame Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `capture_frame` MCP tool and CLI command that launches a target app with RenderDoc injected, captures one frame after N frames delay, and auto-opens it for analysis.

**Architecture:** New `core/capture.h/.cpp` module handles the full pipeline (validate → launch → inject → poll → copy → open). MCP tool in `capture_tools.cpp` wraps it with JSON schema. CLI adds a `capture` subcommand. All follows the existing layered pattern.

**Tech Stack:** C++17, RenderDoc replay API (`RENDERDOC_ExecuteAndInject`, `ITargetControl`, `CaptureOptions`), nlohmann/json, GoogleTest.

---

### Task 1: Add CaptureRequest/CaptureResult types to core/types.h

**Files:**
- Modify: `src/core/types.h`

- [ ] **Step 1: Add the new structs at the end of types.h (before closing namespace brace)**

In `src/core/types.h`, add after the `ExportResult` struct (after line 241, before the closing `}`):

```cpp
// --- Capture ---
struct CaptureRequest {
    std::string exePath;
    std::string workingDir;
    std::string cmdLine;
    uint32_t delayFrames = 100;
    std::string outputPath;
};

struct CaptureResult {
    std::string capturePath;
    uint32_t pid = 0;
};
```

- [ ] **Step 2: Verify it compiles**

Run: `cmake --build build --config Release --target renderdoc-mcp-proto 2>&1 | tail -5`
Expected: Build succeeds (types.h is header-only, included by proto layer)

- [ ] **Step 3: Commit**

```bash
git add src/core/types.h
git commit -m "feat(core): add CaptureRequest and CaptureResult types"
```

---

### Task 2: Add CaptureResult serialization

**Files:**
- Modify: `src/mcp/serialization.h`
- Modify: `src/mcp/serialization.cpp`
- Modify: `tests/unit/test_serialization.cpp`

- [ ] **Step 1: Write the failing test**

In `tests/unit/test_serialization.cpp`, add at the end:

```cpp
TEST(CaptureResultSerialization, BasicFields) {
    core::CaptureResult result;
    result.capturePath = "C:/tmp/test_capture.rdc";
    result.pid = 12345;

    auto j = mcp::to_json(result);
    EXPECT_EQ(j["capturePath"], "C:/tmp/test_capture.rdc");
    EXPECT_EQ(j["pid"], 12345u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Release --target test-unit 2>&1 | tail -10`
Expected: FAIL — `to_json(const CaptureResult&)` is not declared

- [ ] **Step 3: Add declaration in serialization.h**

In `src/mcp/serialization.h`, add after the `to_json(const core::ExportResult&)` line (line 40):

```cpp
nlohmann::json to_json(const core::CaptureResult& result);
```

- [ ] **Step 4: Add implementation in serialization.cpp**

In `src/mcp/serialization.cpp`, add at the end (before the closing namespace brace):

```cpp
nlohmann::json to_json(const core::CaptureResult& r) {
    return {{"capturePath", r.capturePath}, {"pid", r.pid}};
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --config Release --target test-unit && cd build && ctest -R test-unit -C Release --output-on-failure 2>&1 | tail -15`
Expected: All tests pass including `CaptureResultSerialization.BasicFields`

- [ ] **Step 6: Commit**

```bash
git add src/mcp/serialization.h src/mcp/serialization.cpp tests/unit/test_serialization.cpp
git commit -m "feat(mcp): add CaptureResult JSON serialization"
```

---

### Task 3: Implement core::captureFrame

**Files:**
- Create: `src/core/capture.h`
- Create: `src/core/capture.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create the header file src/core/capture.h**

```cpp
#pragma once

#include "core/types.h"
#include <string>

namespace renderdoc::core {

class Session;

CaptureResult captureFrame(Session& session, const CaptureRequest& req);

} // namespace renderdoc::core
```

- [ ] **Step 2: Create src/core/capture.cpp**

```cpp
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
        throw CoreError(CoreError::Code::InvalidArgument,
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

    // 10. Cleanup — guard destructor calls ctrl->Shutdown()

    // 11. Auto-open the capture
    guard.c = nullptr; // prevent double-shutdown; we're done with ctrl
    ctrl->Shutdown();
    session.open(outputPath);

    return CaptureResult{outputPath, pid};
}

} // namespace renderdoc::core
```

- [ ] **Step 3: Make Session::ensureReplayInitialized() accessible**

In `src/core/session.h`, move `ensureReplayInitialized()` from `private` to `public`:

Move this line from the private section to the public section (after `bool isOpen() const;`):

```cpp
    void ensureReplayInitialized();
```

- [ ] **Step 4: Add capture.cpp to CMakeLists.txt**

In `CMakeLists.txt`, add `src/core/capture.cpp` to the `renderdoc-core` library source list. After `src/core/export.cpp` (line 37):

```
        src/core/capture.cpp
```

- [ ] **Step 5: Verify it compiles**

Run: `cmake --build build --config Release --target renderdoc-core 2>&1 | tail -10`
Expected: Build succeeds

- [ ] **Step 6: Commit**

```bash
git add src/core/capture.h src/core/capture.cpp src/core/session.h CMakeLists.txt
git commit -m "feat(core): implement captureFrame — launch, inject, capture, auto-open"
```

---

### Task 4: Add stub for captureFrame in session_stub.cpp

**Files:**
- Modify: `tests/unit/session_stub.cpp`

- [ ] **Step 1: Add the stub**

In `tests/unit/session_stub.cpp`, add at the top of the file (after the existing includes):

```cpp
#include "core/capture.h"
```

Then add at the end, before the closing `} // namespace renderdoc::core`:

```cpp
CaptureResult captureFrame(Session&, const CaptureRequest&) {
    return CaptureResult{"stub.rdc", 0};
}
```

Also add the `ensureReplayInitialized` stub if not already present (check the existing stub — it's already there on line 39).

- [ ] **Step 2: Verify unit tests still build and pass**

Run: `cmake --build build --config Release --target test-unit && cd build && ctest -R test-unit -C Release --output-on-failure 2>&1 | tail -10`
Expected: All tests pass

- [ ] **Step 3: Commit**

```bash
git add tests/unit/session_stub.cpp
git commit -m "test: add captureFrame stub for unit tests"
```

---

### Task 5: Register capture_frame MCP tool

**Files:**
- Create: `src/mcp/tools/capture_tools.cpp`
- Modify: `src/mcp/tools/tools.h`
- Modify: `src/mcp/mcp_server_default.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create src/mcp/tools/capture_tools.cpp**

```cpp
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
        "and automatically open it for analysis. Returns capture info (API, event count) "
        "and the .rdc file path.",
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
```

- [ ] **Step 2: Add declaration to tools.h**

In `src/mcp/tools/tools.h`, add after `void registerShaderTools(ToolRegistry& registry);` (line 14):

```cpp
void registerCaptureTools(ToolRegistry& registry);
```

- [ ] **Step 3: Register in mcp_server_default.cpp**

In `src/mcp/mcp_server_default.cpp`, add after `tools::registerExportTools(*m_registry);` (line 21):

```cpp
    tools::registerCaptureTools(*m_registry);
```

- [ ] **Step 4: Add to CMakeLists.txt**

In `CMakeLists.txt`, add `src/mcp/tools/capture_tools.cpp` to the `renderdoc-mcp-lib` source list. After `src/mcp/tools/export_tools.cpp` (line 88):

```
        src/mcp/tools/capture_tools.cpp
```

- [ ] **Step 5: Verify full build**

Run: `cmake --build build --config Release 2>&1 | tail -10`
Expected: Build succeeds — renderdoc-mcp.exe includes the new tool

- [ ] **Step 6: Commit**

```bash
git add src/mcp/tools/capture_tools.cpp src/mcp/tools/tools.h src/mcp/mcp_server_default.cpp CMakeLists.txt
git commit -m "feat(mcp): register capture_frame tool"
```

---

### Task 6: Add capture CLI command

**Files:**
- Modify: `src/cli/main.cpp`

- [ ] **Step 1: Add capture include**

In `src/cli/main.cpp`, add after the existing includes (after line 22):

```cpp
#include "core/capture.h"
```

- [ ] **Step 2: Add the capture command implementation**

Add a new function after `cmdExportRt` (after line 293):

```cpp
static void cmdCapture(Session& session, const std::string& exePath,
                       const std::string& workingDir, const std::string& cmdLineArgs,
                       uint32_t delayFrames, const std::string& outputPath) {
    CaptureRequest req;
    req.exePath = exePath;
    req.workingDir = workingDir;
    req.cmdLine = cmdLineArgs;
    req.delayFrames = delayFrames;
    req.outputPath = outputPath;

    std::cerr << "Launching and injecting: " << exePath << "\n";
    std::cerr << "Waiting " << delayFrames << " frames before capture...\n";

    CaptureResult result = captureFrame(session, req);

    CaptureInfo info = getCaptureInfo(session);

    std::cout << "Capture:      " << result.capturePath << "\n"
              << "PID:          " << result.pid << "\n"
              << "API:          " << apiName(info.api) << "\n"
              << "Total events: " << info.totalEvents << "\n"
              << "Total draws:  " << info.totalDraws << "\n";
}
```

- [ ] **Step 3: Update Args struct and parser**

In the `Args` struct (around line 72), add new fields:

```cpp
    std::string cmdLineArgs;     // --args for capture command
    uint32_t delayFrames = 100;  // --delay-frames for capture
```

In the `printUsage` function, add after the `export-rt` line:

```cpp
              << "  capture EXE [-w DIR] [-a ARGS] [-d N] [-o PATH]\n"
```

In `parseArgs`, add parsing for the new flags. In the while loop, add before the final `else` branch:

```cpp
        } else if ((tok == "-a" || tok == "--args") && i + 1 < argc) {
            a.cmdLineArgs = argv[++i];
        } else if ((tok == "-d" || tok == "--delay-frames") && i + 1 < argc) {
            a.delayFrames = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if ((tok == "-w" || tok == "--working-dir") && i + 1 < argc) {
            // reuse outputDir for working-dir (capture command only)
            a.filter = argv[++i]; // repurpose filter for working-dir in capture context
```

Actually, let's keep it cleaner. Add a `workingDir` field to Args:

In the `Args` struct, add:

```cpp
    std::string workingDir;
```

In the parser while loop, add:

```cpp
        } else if ((tok == "-w" || tok == "--working-dir") && i + 1 < argc) {
            a.workingDir = argv[++i];
        } else if ((tok == "-a" || tok == "--args") && i + 1 < argc) {
            a.cmdLineArgs = argv[++i];
        } else if ((tok == "-d" || tok == "--delay-frames") && i + 1 < argc) {
            a.delayFrames = static_cast<uint32_t>(std::stoul(argv[++i]));
```

- [ ] **Step 4: Handle the capture command in main**

In the command dispatch block in `main()`, the `capture` command does NOT need a pre-opened capture file. Restructure main to handle `capture` before opening a capture file.

Replace the main function (starting at line 299) with:

```cpp
int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    Args args = parseArgs(argc, argv);

    Session session;

    try {
        // capture command: first arg is exe path, not a .rdc file
        if (args.command == "capture") {
            if (args.positional.empty()) {
                std::cerr << "error: 'capture' requires an executable path\n";
                return 1;
            }
            cmdCapture(session, args.positional[0], args.workingDir,
                       args.cmdLineArgs, args.delayFrames, args.outputDir);
            session.close();
            return 0;
        }

        // All other commands require opening a capture first
        session.open(args.capturePath);
    } catch (const CoreError& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    try {
        const std::string& cmd = args.command;

        if (cmd == "info") {
            cmdInfo(session);
        } else if (cmd == "events") {
            cmdEvents(session, args.filter);
        } else if (cmd == "draws") {
            cmdDraws(session, args.filter);
        } else if (cmd == "pipeline") {
            cmdPipeline(session, args.eventId);
        } else if (cmd == "shader") {
            if (args.positional.empty()) {
                std::cerr << "error: 'shader' requires a stage argument (vs|hs|ds|gs|ps|cs)\n";
                return 1;
            }
            cmdShader(session, args.positional[0], args.eventId);
        } else if (cmd == "resources") {
            cmdResources(session, args.typeFilter);
        } else if (cmd == "export-rt") {
            cmdExportRt(session, args.positional, args.outputDir, args.eventId);
        } else {
            std::cerr << "error: unknown command '" << cmd << "'\n\n";
            printUsage(argv[0]);
            return 1;
        }
    } catch (const CoreError& e) {
        std::cerr << "error: " << e.what() << "\n";
        session.close();
        return 1;
    }

    session.close();
    return 0;
}
```

Wait — the current CLI uses `argv[1]` as capture path and `argv[2]` as command. For the `capture` command the usage should be `renderdoc-cli capture <exe> [options]`. This means we need to adjust parsing so `capture` is recognized as a command where argv[1] is the command and argv[2]+positionals are the exe.

Let's simplify: Keep the existing `argv[1]=capturePath, argv[2]=command` pattern, but for `capture` specifically, argv[1] is the command and the exe goes in positional. Update `parseArgs` and `main`:

Replace the entire `parseArgs` and `main` to handle this:

In `parseArgs`, change the initial assignment so it can handle `capture` being argv[1]:

```cpp
static Args parseArgs(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        std::exit(1);
    }

    Args a;

    // Special case: "capture" command doesn't take a .rdc as first arg
    if (std::string(argv[1]) == "capture") {
        a.command = "capture";
        int i = 2;
        while (i < argc) {
            std::string tok = argv[i];
            if ((tok == "-w" || tok == "--working-dir") && i + 1 < argc) {
                a.workingDir = argv[++i];
            } else if ((tok == "-a" || tok == "--args") && i + 1 < argc) {
                a.cmdLineArgs = argv[++i];
            } else if ((tok == "-d" || tok == "--delay-frames") && i + 1 < argc) {
                a.delayFrames = static_cast<uint32_t>(std::stoul(argv[++i]));
            } else if ((tok == "-o" || tok == "--output") && i + 1 < argc) {
                a.outputDir = argv[++i];
            } else {
                a.positional.push_back(tok);
            }
            ++i;
        }
        return a;
    }

    // Standard commands: <capture.rdc> <command> [options]
    if (argc < 3) {
        printUsage(argv[0]);
        std::exit(1);
    }

    a.capturePath = argv[1];
    a.command     = argv[2];

    int i = 3;
    while (i < argc) {
        std::string tok = argv[i];
        if ((tok == "-e" || tok == "--event") && i + 1 < argc) {
            a.eventId = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--filter" && i + 1 < argc) {
            a.filter = argv[++i];
        } else if (tok == "--type" && i + 1 < argc) {
            a.typeFilter = argv[++i];
        } else if (tok == "-o" && i + 1 < argc) {
            a.outputDir = argv[++i];
        } else {
            a.positional.push_back(tok);
        }
        ++i;
    }

    return a;
}
```

And update `main()` as shown above.

- [ ] **Step 5: Verify it compiles**

Run: `cmake --build build --config Release --target renderdoc-cli 2>&1 | tail -10`
Expected: Build succeeds

- [ ] **Step 6: Commit**

```bash
git add src/cli/main.cpp
git commit -m "feat(cli): add capture command to renderdoc-cli"
```

---

### Task 7: Update SKILL.md

**Files:**
- Modify: `skill/SKILL.md`

- [ ] **Step 1: Add capture workflow section**

In `skill/SKILL.md`, add a new section after "Quick Start" (after line 6) and before "Common Debugging Scenarios":

```markdown

## Frame Capture Workflow

Use `capture_frame` to launch a target application, inject RenderDoc, and capture a frame — all in one step. The capture is automatically opened for analysis.

1. `capture_frame` — Launch target app with RenderDoc injected, capture after N frames
2. `get_capture_info` — Understand API, event count, GPU info
3. `list_draws` — Survey draw calls in the frame
4. `goto_event` + `get_pipeline_state` — Inspect a specific draw
5. `get_shader` — Read shader disassembly
6. `export_render_target` — Export visual results
```

- [ ] **Step 2: Add capture_frame to the Tool Reference table**

In the Tool Reference table, add as the first row (before `open_capture`):

```markdown
| `capture_frame` | Launch app, capture frame, auto-open |
```

- [ ] **Step 3: Commit**

```bash
git add skill/SKILL.md
git commit -m "docs: add capture_frame to SKILL.md workflow and tool reference"
```

---

### Task 8: Manual integration test

**Files:**
- Create: `tests/integration/test_capture.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the integration test file**

```cpp
#include <gtest/gtest.h>
#include "core/capture.h"
#include "core/info.h"
#include "core/session.h"

#include <filesystem>

using namespace renderdoc::core;

// This test requires a real graphics application and RenderDoc.
// It is NOT run in CI. Run manually with:
//   test-tools --gtest_filter="CaptureFrame.*"
//
// Set environment variable RENDERDOC_TEST_EXE to the path of a graphics app.
// Example: set RENDERDOC_TEST_EXE=C:\Windows\System32\mspaint.exe

class CaptureFrameTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* exe = std::getenv("RENDERDOC_TEST_EXE");
        if (!exe || !std::filesystem::exists(exe)) {
            GTEST_SKIP() << "Set RENDERDOC_TEST_EXE to a graphics app for capture tests";
        }
        testExe = exe;
    }

    std::string testExe;
    Session session;
};

TEST_F(CaptureFrameTest, CaptureAndOpen) {
    CaptureRequest req;
    req.exePath = testExe;
    req.delayFrames = 5; // low delay for test speed

    CaptureResult result = captureFrame(session, req);

    EXPECT_FALSE(result.capturePath.empty());
    EXPECT_TRUE(std::filesystem::exists(result.capturePath));
    EXPECT_GT(result.pid, 0u);

    // Session should be open with valid capture
    EXPECT_TRUE(session.isOpen());

    CaptureInfo info = getCaptureInfo(session);
    EXPECT_GT(info.totalEvents, 0u);

    // Cleanup
    session.close();
    std::filesystem::remove(result.capturePath);
}

TEST_F(CaptureFrameTest, InvalidExePath) {
    CaptureRequest req;
    req.exePath = "C:/nonexistent/app.exe";

    EXPECT_THROW(captureFrame(session, req), CoreError);
}
```

- [ ] **Step 2: Add to tests/CMakeLists.txt**

In `tests/CMakeLists.txt`, the `test-tools` target already uses a glob pattern `integration/test_tools*.cpp`. The new file is `test_capture.cpp`, which doesn't match the glob. Add it explicitly.

After the `test-tools` target block (after line 35), add:

```cmake
    add_executable(test-capture integration/test_capture.cpp)
    target_link_libraries(test-capture PRIVATE renderdoc-core GTest::gtest_main)
    gtest_discover_tests(test-capture PROPERTIES LABELS integration DISCOVERY_MODE PRE_TEST)
    if(RENDERDOC_DLL)
        add_custom_command(TARGET test-capture POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RENDERDOC_DLL}" "$<TARGET_FILE_DIR:test-capture>")
    endif()
```

- [ ] **Step 3: Verify it compiles**

Run: `cmake --build build --config Release --target test-capture 2>&1 | tail -10`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add tests/integration/test_capture.cpp tests/CMakeLists.txt
git commit -m "test: add manual integration test for captureFrame"
```

---

### Task 9: Verify full build and unit tests

- [ ] **Step 1: Clean build all targets**

Run: `cmake --build build --config Release 2>&1 | tail -20`
Expected: All targets build successfully

- [ ] **Step 2: Run unit tests**

Run: `cd build && ctest -R test-unit -C Release --output-on-failure`
Expected: All unit tests pass

- [ ] **Step 3: Verify capture_frame is listed in MCP tool definitions**

Run a quick smoke test by starting renderdoc-mcp and sending a tools/list request:

```bash
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' | build/Release/renderdoc-mcp.exe 2>/dev/null | grep capture_frame
```

Expected: Output includes `"capture_frame"` in the tools list

- [ ] **Step 4: Final commit if any cleanup needed**

```bash
git add -A
git status
# Only commit if there are changes
```
