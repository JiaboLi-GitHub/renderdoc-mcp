# capture_frame — Automatic Frame Capture Design Spec

## Overview

Add a `capture_frame` MCP tool and CLI command that launches a target application with RenderDoc injected, waits a specified number of frames, captures a single frame, copies the `.rdc` file locally, and automatically opens it for analysis — all in one synchronous operation.

**Goal**: Developers can say "capture a frame from my app" in a Claude conversation and immediately start analyzing the result, without touching the RenderDoc GUI.

## Environment

- RenderDoc source: `D:\renderdoc\renderdoc`
- RenderDoc installed: `C:\ProgramData\Microsoft\Windows\Start Menu\Programs\RenderDoc`
- renderdoc-mcp project: `D:\renderdoc\renderdoc-mcp`

## MCP Tool Interface

### `capture_frame`

```json
{
  "name": "capture_frame",
  "description": "Launch an application with RenderDoc injected, capture a frame after a delay, and automatically open it for analysis.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "exePath":     { "type": "string",  "description": "Absolute path to the target executable" },
      "workingDir":  { "type": "string",  "description": "Working directory for the target. Defaults to exePath's parent directory" },
      "cmdLine":     { "type": "string",  "description": "Command line arguments for the target. Default: empty" },
      "delayFrames": { "type": "integer", "description": "Number of frames to wait before capturing. Default: 100" },
      "outputPath":  { "type": "string",  "description": "Path for the .rdc file. Default: auto-generated in temp directory" }
    },
    "required": ["exePath"]
  }
}
```

**Return value** (same shape as `open_capture`):

```json
{
  "api": "D3D11",
  "eventCount": 1234,
  "path": "C:/Users/.../renderdoc-mcp/app_20260331_143022.rdc"
}
```

After `capture_frame` returns, the capture is already open in the session — the caller can immediately use `list_draws`, `get_pipeline_state`, `get_shader`, etc.

**CaptureOptions defaults** (not exposed as parameters in v1):

| Option | Value | Rationale |
|--------|-------|-----------|
| `allowVSync` | `true` | Don't interfere with app behavior |
| `allowFullscreen` | `true` | Don't interfere with app behavior |
| `apiValidation` | `false` | Avoid performance overhead |
| `captureCallstacks` | `false` | Avoid performance overhead |
| `captureCallstacksOnlyActions` | `false` | N/A when callstacks disabled |
| `delayForDebugger` | `0` | No debugger attach |
| `hookIntoChildren` | `false` | Only capture the target process |
| `verifyBufferAccess` | `false` | Avoid performance overhead |
| `refAllResources` | `false` | Only capture referenced resources |
| `captureAllCmdLists` | `false` | Not needed for single-frame capture |
| `debugOutputMute` | `true` | Reduce noise |
| `softMemoryLimit` | `0` | No limit |

## Core Layer

### New module: `src/core/capture.h` / `src/core/capture.cpp`

```cpp
namespace core {

struct CaptureRequest {
    std::string exePath;      // Required: absolute path to target exe
    std::string workingDir;   // Default: exePath's parent directory
    std::string cmdLine;      // Default: ""
    uint32_t delayFrames;     // Default: 100
    std::string outputPath;   // Default: auto-generated
};

struct CaptureResult {
    std::string capturePath;  // Final .rdc file path
    uint32_t pid;             // Target process PID
};

// Full synchronous pipeline: launch → inject → wait → capture → copy → open
CaptureResult captureFrame(Session& session, const CaptureRequest& req);

}  // namespace core
```

### Internal flow of `captureFrame()`

1. **Validate inputs**
   - `exePath` must exist as a file (`std::filesystem::exists`)
   - If `workingDir` is empty, derive from `exePath`'s parent directory
   - If `workingDir` is specified, it must exist as a directory

2. **Generate output path** (if `outputPath` is empty)
   - Pattern: `<temp_dir>/renderdoc-mcp/<exeName>_<YYYYMMDD_HHMMSS>.rdc`
   - Create the `renderdoc-mcp` subdirectory if it doesn't exist

3. **Configure CaptureOptions**
   - Zero-initialize `CaptureOptions`, then set defaults per table above
   - Capture file template: pass `outputPath` without `.rdc` extension as the `capturefile` parameter to `ExecuteAndInject`. RenderDoc uses this as a prefix and generates files like `<template>_frame123.rdc` on the target side. The actual file is later retrieved via `CopyCapture` to our exact `outputPath`.

4. **Launch and inject**
   ```cpp
   ExecuteResult result = RENDERDOC_ExecuteAndInject(
       exePath, workingDir, cmdLine,
       {},           // no env modifications
       captureFileTemplate,
       opts,
       false         // don't wait for exit
   );
   ```
   - If `result.result.code != ResultCode::Succeeded` → throw `CoreError::InternalError`
   - Store `result.ident` for target control connection

5. **Connect target control**
   ```cpp
   ITargetControl* ctrl = RENDERDOC_CreateTargetControl(
       "",                // localhost
       result.ident,      // from ExecuteAndInject
       "renderdoc-mcp",   // client name
       true               // force connection
   );
   ```
   - If `ctrl == nullptr` → throw `CoreError::InternalError`
   - RAII wrapper: ensure `ctrl->Shutdown()` is called on all exit paths

6. **Queue capture**
   ```cpp
   ctrl->QueueCapture(delayFrames, 1);  // capture 1 frame at frame N
   ```

7. **Poll for completion**
   - Loop calling `ctrl->ReceiveMessage(nullptr)` with a 60-second wall-clock timeout
   - On `TargetControlMessageType::NewCapture`: store `captureId`, break
   - On disconnect/error: throw `CoreError::InternalError`
   - On timeout: throw `CoreError::InternalError, "Capture timed out after 60 seconds"`
   - Sleep 100ms between polls to avoid busy-waiting

8. **Copy capture file**
   ```cpp
   ctrl->CopyCapture(captureId, outputPath);
   ```
   - Poll `ReceiveMessage` again to wait for copy completion if needed

9. **Cleanup connection**
   ```cpp
   ctrl->Shutdown();
   ```
   - Does NOT kill the target process — it continues running

10. **Auto-open capture**
    ```cpp
    session.open(outputPath);
    ```
    - This closes any previously open capture and loads the new one

11. **Return result**
    ```cpp
    return CaptureResult{ outputPath, ctrl->GetPID() };
    ```

### Error handling

| Condition | Error |
|-----------|-------|
| `exePath` doesn't exist | `CoreError::InvalidArgument, "Target executable not found: <path>"` |
| `workingDir` doesn't exist | `CoreError::InvalidArgument, "Working directory not found: <path>"` |
| `ExecuteAndInject` fails | `CoreError::InternalError, "Failed to launch and inject: <message>"` |
| `CreateTargetControl` returns null | `CoreError::InternalError, "Failed to connect to target process"` |
| Target disconnects before capture | `CoreError::InternalError, "Target process exited before capture completed"` |
| 60-second timeout | `CoreError::InternalError, "Capture timed out after 60 seconds"` |
| `CopyCapture` / file not created | `CoreError::InternalError, "Failed to copy capture file"` |

## MCP Tool Layer

### New file: `src/mcp/tools/capture_tools.cpp`

```cpp
void registerCaptureTools(mcp::ToolRegistry& registry) {
    registry.registerTool({
        "capture_frame",
        "Launch an application with RenderDoc injected, capture a frame after a delay, "
        "and automatically open it for analysis.",
        inputSchema,  // as defined above
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            core::CaptureRequest req;
            req.exePath = args.at("exePath").get<std::string>();
            req.workingDir = args.value("workingDir", "");
            req.cmdLine = args.value("cmdLine", "");
            req.delayFrames = args.value("delayFrames", 100);
            req.outputPath = args.value("outputPath", "");

            auto result = core::captureFrame(session, req);

            // Session is now open — return same format as open_capture
            auto info = core::getCaptureInfo(session);
            auto j = to_json(info);
            j["path"] = result.capturePath;
            return j;
        }
    });
}
```

### Registration

In `mcp_server_default.cpp`, add `registerCaptureTools(registry)` alongside existing tool registrations.

### Serialization

Add `to_json(const CaptureResult&)` in `serialization.h/.cpp`:

```cpp
nlohmann::json to_json(const core::CaptureResult& r) {
    return {{"capturePath", r.capturePath}, {"pid", r.pid}};
}
```

## CLI Command

In `src/cli/main.cpp`, add `capture` subcommand:

```
renderdoc-cli capture <exe> [options]
  -w, --working-dir DIR    Working directory (default: exe's parent)
  -a, --args "ARGS"        Command line arguments
  -d, --delay-frames N     Frames to wait before capture (default: 100)
  -o, --output PATH        Output .rdc file path (default: auto)
```

Implementation: parse args → construct `CaptureRequest` → call `core::captureFrame(session, req)` → print result as JSON (consistent with other CLI commands).

## Build System

### CMakeLists.txt changes

- Add `src/core/capture.cpp` to the `renderdoc-core` library source list
- Add `src/mcp/tools/capture_tools.cpp` to the `renderdoc-mcp-lib` source list
- No new external dependencies — `ITargetControl`, `RENDERDOC_ExecuteAndInject`, `RENDERDOC_CreateTargetControl` are all declared in `renderdoc_replay.h` and exported from `renderdoc.lib`

## Testing

### Unit tests (no RenderDoc dependency)

In `tests/unit/test_capture.cpp`:
- Parameter validation: missing exePath, non-existent exePath, non-existent workingDir
- Default value derivation: workingDir from exePath, output path generation
- Serialization: `to_json(CaptureResult)` round-trip

### Integration tests (manual, requires RenderDoc + graphics app)

In `tests/integration/test_capture.cpp`:
- Full capture workflow with a simple test application
- Not run in CI — requires GPU and a renderable target process

### Serialization tests

Add `CaptureResult` serialization test to existing `tests/unit/test_serialization.cpp`.

## SKILL.md Update

Add capture workflow to the recommended flow:

```
## Frame Capture Workflow
1. capture_frame — Launch target app, inject RenderDoc, capture a frame
2. get_capture_info — Understand API, event count, GPU
3. list_draws — Survey draw calls
4. goto_event + get_pipeline_state — Inspect specific draw
5. get_shader — Read disassembly
6. export_render_target — Export visual results
```

## Scope Exclusions (v1)

- No `CaptureOptions` parameter exposure — fixed defaults only
- No attach-to-running-process support — launch-and-inject only
- No multi-frame capture — single frame only
- No process lifecycle management — target process is not killed after capture
- No remote capture — localhost only
- No progress callback — synchronous blocking call
