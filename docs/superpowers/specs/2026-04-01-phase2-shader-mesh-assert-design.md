# Phase 2 Design: Shader Editing, Extended Export, CI Assertions

**Date:** 2026-04-01
**Status:** Draft
**Scope:** 13 new MCP tools (40 total)

## Overview

Phase 2 adds three capability groups to renderdoc-mcp:

1. **Shader Hot-Editing** (5 tools) — compile, replace, and restore shaders at runtime
2. **Extended Export** (3 tools) — mesh data, draw snapshots, resource usage tracking
3. **CI Assertions** (5 tools) — automated validation of render state and pixel values

Architecture follows existing core/mcp/cli layered pattern. No changes to existing 27 tools.

---

## 1. Shader Hot-Editing

### 1.1 New Types (`types.h`)

```cpp
enum class ShaderEncoding {
    Unknown = 0, DXBC = 1, GLSL = 2, SPIRV = 3,
    SPIRVAsm = 4, HLSL = 5, DXIL = 6,
    OpenGLSPIRV = 7, OpenGLSPIRVAsm = 8, Slang = 9
};

enum class ShaderStage {
    Vertex = 0, Hull = 1, Domain = 2,
    Geometry = 3, Pixel = 4, Compute = 5
};

struct ShaderBuildResult {
    uint64_t shader_id;    // 0 = failure
    std::string warnings;  // compiler warnings or error message
};
```

### 1.2 Core Module (`shader_edit.h/cpp`)

State tracked within session:
- `built_shaders: map<uint64_t, ResourceId>` — compiled shaders by shader_id
- `shader_replacements: map<uint64_t, ResourceId>` — active replacements by original_rid

Cleanup on session close: iterate both maps, call `RemoveReplacement()` then `FreeTargetResource()`.

Pipeline state cache invalidated after every replace/restore operation.

### 1.3 RenderDoc APIs

| Function | API Call |
|----------|---------|
| `getShaderEncodings()` | `controller->GetTargetShaderEncodings()` |
| `buildShader(source, stage, entry, encoding)` | `controller->BuildTargetShader(entry, encoding, source_bytes, flags, stage)` |
| `replaceShader(eventId, stage, shaderId)` | `controller->ReplaceResource(original_rid, built_rid)` |
| `restoreShader(eventId, stage)` | `controller->RemoveReplacement(original_rid)` |
| `restoreAllShaders()` | Loop: `RemoveReplacement()` + `FreeTargetResource()` |

### 1.4 MCP Tools

#### `shader_encodings`
- **Parameters:** none
- **Returns:** `{encodings: string[]}` — list of encoding names supported by current capture

#### `shader_build`
- **Parameters:**
  - `source` (string, required) — shader source code
  - `stage` (string, required) — `"vs"|"hs"|"ds"|"gs"|"ps"|"cs"`
  - `entry` (string, optional, default `"main"`) — entry point name
  - `encoding` (string, optional, default `"SPIRV"`) — encoding name (case-insensitive)
- **Returns:** `{shaderId: int, warnings: string}`
- **Errors:** build failure returns error with compiler message

#### `shader_replace`
- **Parameters:**
  - `eventId` (int, required) — event ID to locate original shader
  - `stage` (string, required) — shader stage
  - `shaderId` (int, required) — from shader_build result
- **Returns:** `{originalId: int, message: string}`
- **Notes:** replacement affects ALL draws using the same shader globally

#### `shader_restore`
- **Parameters:**
  - `eventId` (int, required) — event ID
  - `stage` (string, required) — shader stage
- **Returns:** `{restored: true}`
- **Errors:** no active replacement at specified stage

#### `shader_restore_all`
- **Parameters:** none
- **Returns:** `{restoredCount: int, freedCount: int}`

### 1.5 CLI Commands

| Command | Maps to |
|---------|---------|
| `shader-encodings` | `shader_encodings` |
| `shader-build FILE --stage STAGE [--entry NAME] [--encoding ENC]` | `shader_build` (reads source from file) |
| `shader-replace EID STAGE --with SHADER_ID` | `shader_replace` |
| `shader-restore EID STAGE` | `shader_restore` |
| `shader-restore-all` | `shader_restore_all` |

---

## 2. Extended Export

### 2.1 Mesh Export

#### New Types (`types.h`)

```cpp
enum class MeshStage { VSOut = 1, GSOut = 2 };

enum class MeshTopology { TriangleList, TriangleStrip, TriangleFan, Other };

struct MeshVertex {
    float x, y, z;
};

struct MeshData {
    uint32_t eventId;
    MeshStage stage;
    MeshTopology topology;
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<std::array<uint32_t, 3>> faces;  // generated triangles
};
```

#### Core Module (`mesh.h/cpp`)

RenderDoc API calls:
1. `controller->GetPostVSData(0, 0, stage_val)` — mesh metadata (vertex/index buffer IDs, strides, topology)
2. `controller->GetBufferData(rid, offset, size)` — raw buffer bytes

Vertex decoding:
- 4-byte component → IEEE 754 float
- 2-byte component → half-float
- 1-byte component → normalized (value / 255.0)
- Extract first 3 components (xyz) per vertex

Face generation from topology:
- TriangleList: group indices [0,1,2], [3,4,5], ...
- TriangleStrip: [i, i+1, i+2] with alternating winding
- TriangleFan: [0, i, i+1] for each i

OBJ output format:
```
# rdc mesh export: eid=N stage=vs-out vertices=M faces=K topology=TriangleList
v x y z
v x y z
...
f 1 2 3
f 4 5 6
...
```

#### MCP Tool: `export_mesh`

- **Parameters:**
  - `eventId` (int, required) — draw call event ID
  - `stage` (string, optional, default `"vs-out"`) — `"vs-out"` or `"gs-out"`
  - `format` (string, optional, default `"obj"`) — `"obj"` or `"json"`
  - `outputPath` (string, optional) — file path; if omitted returns content inline
- **Returns:** OBJ text string, or JSON `{eventId, stage, topology, vertexCount, faceCount, vertices, indices, faces}`

#### CLI Command

`mesh EID [--stage vs-out|gs-out] [--format obj|json] [-o FILE]`

### 2.2 Draw Snapshot

#### Core Module (`snapshot.h/cpp`)

Aggregates existing core functions to export complete draw state:

```cpp
struct SnapshotResult {
    std::string manifestPath;
    std::vector<std::string> files;  // all exported file paths
    std::vector<std::string> errors; // non-fatal errors (skipped items)
};

SnapshotResult exportSnapshot(uint32_t eventId, const std::string& outputDir);
```

Export sequence:
1. **pipeline.json** — `getPipelineState(eventId)` serialized to JSON (fatal if fails)
2. **shader_{stage}.txt** — `getShader(eventId, stage, "disasm")` for each active stage (skip on failure)
3. **color{i}.png** — `exportRenderTarget(eventId, i)` for targets 0-7 (stop on first not-found)
4. **depth.png** — export depth target (skip on failure)
5. **manifest.json** — index with `{eventId, timestamp, files[]}`

#### MCP Tool: `export_snapshot`

- **Parameters:**
  - `eventId` (int, required) — draw call event ID
  - `outputDir` (string, required) — output directory path
- **Returns:** `{manifestPath: string, files: string[], errors: string[]}`

#### CLI Command

`snapshot EID -o DIR`

### 2.3 Resource Usage

#### Core Module (`usage.h/cpp`)

```cpp
struct ResourceUsageEntry {
    uint32_t eventId;
    std::string usage;  // "OutputTarget", "InputTarget", "ReadOnly", etc.
};

struct ResourceUsageResult {
    uint64_t resourceId;
    std::vector<ResourceUsageEntry> entries;
};
```

RenderDoc API: `controller->GetUsage(resourceId)` returns `EventUsage[]` with `eventId` and `usage` enum.

Usage enum names: `VertexBuffer`, `IndexBuffer`, `InputTarget`, `OutputTarget`, `ReadOnly`, `ReadWrite`, `CopySrc`, `CopyDst`, `Resolve`, `ResolveSrc`, `ResolveDst`, `Clear`, etc.

#### MCP Tool: `get_resource_usage`

- **Parameters:**
  - `resourceId` (int, required) — resource ID
- **Returns:** `{resourceId: int, entries: [{eventId: int, usage: string}]}`

#### CLI Command

`usage RESOURCE_ID`

---

## 3. CI Assertions

All assertion tools return structured results without throwing exceptions. Return format:

```json
{
  "pass": true|false,
  "message": "human-readable summary",
  ...tool-specific fields
}
```

### 3.1 assert_pixel

Validates pixel RGBA value at coordinates against expected value with tolerance.

- **Parameters:**
  - `eventId` (int, required)
  - `x`, `y` (int, required) — pixel coordinates
  - `expected` (float[4], required) — `[R, G, B, A]`
  - `tolerance` (float, optional, default 0.01) — per-channel tolerance
  - `target` (int, optional, default 0) — render target index
- **Returns:** `{pass, actual: float[4], expected: float[4], tolerance, message}`

**Implementation:** Reuses `pixelHistory()`. Finds last passing modification, extracts `post_mod` RGBA. Per-channel: `|actual[i] - expected[i]| <= tolerance`.

### 3.2 assert_state

Validates a pipeline state field matches expected value.

- **Parameters:**
  - `eventId` (int, required)
  - `path` (string, required) — dot-separated path, e.g. `"blend.enabled"`, `"viewport.width"`, `"stencil.front.func"`
  - `expected` (string, required) — expected value (string comparison, bools lowercased)
- **Returns:** `{pass, actual: string, expected: string, path, message}`

**Implementation:** Calls `getPipelineState()`, serializes to JSON, then navigates by path with `.` separator and `[N]` array index support. Converts value to string for comparison (bool → lowercase).

**Available paths** (based on current PipelineState struct):
- `"api"` → graphics API name
- `"shaders[0].stage"`, `"shaders[0].shaderId"`, `"shaders[0].entryPoint"` → shader bindings
- `"renderTargets[0].resourceId"`, `"renderTargets[0].format"`, `"renderTargets[0].width"` → RT info
- `"depthTarget.format"` → depth target fields
- `"viewports[0].x"`, `"viewports[0].width"` → viewport fields

**Path navigation rules:**
- `"api"` → top-level field
- `"depthTarget.format"` → nested field
- `"shaders[0].stage"` → array index then field

**Note:** Available paths are limited to what the current `PipelineState` struct exposes. Extending PipelineState with more fields (blend, stencil, rasterizer) is a future enhancement.

### 3.3 assert_image

Compares two PNG images pixel-by-pixel.

- **Parameters:**
  - `expectedPath` (string, required) — path to expected PNG
  - `actualPath` (string, required) — path to actual PNG
  - `threshold` (float, optional, default 0.0) — max diff ratio % to pass
  - `diffOutputPath` (string, optional) — write diff visualization PNG
- **Returns:** `{pass, diffPixels: int, totalPixels: int, diffRatio: float, diffOutputPath?, message}`

**Implementation:**
1. Load both PNGs via stb_image (new dependency)
2. Verify dimensions match (fail immediately if mismatch)
3. Compare RGBA per pixel; any channel difference marks pixel as changed
4. `diffRatio = diffPixels / totalPixels * 100`
5. `pass = diffRatio <= threshold`
6. If diffOutputPath specified: grayscale expected + red overlay on diff pixels

**New dependency:** `stb_image.h` (header-only, add to third_party/ or via FetchContent)

### 3.4 assert_count

Validates count of resources/events/draws against expected value.

- **Parameters:**
  - `what` (string, required) — `"draws"`, `"events"`, `"textures"`, `"buffers"`, `"passes"`
  - `expected` (int, required) — expected count
  - `op` (string, optional, default `"eq"`) — `"eq"|"gt"|"lt"|"ge"|"le"`
- **Returns:** `{pass, actual: int, expected: int, op, message}`

**Implementation:** Calls corresponding list function, counts results, applies comparison operator.

### 3.5 assert_clean

Validates no debug/validation messages at or above specified severity.

- **Parameters:**
  - `minSeverity` (string, optional, default `"high"`) — `"high"|"medium"|"low"|"info"`
- **Returns:** `{pass, count: int, minSeverity, messages: [{severity, eventId, message}]}`

**Implementation:** Reuses `getLog()`. Filters by severity level. Pass if count == 0.

**Severity ranking:** High > Medium > Low > Info

---

## 4. New Files

### Core Layer (`src/core/`)

| File | Lines (est.) | Purpose |
|------|-------------|---------|
| `shader_edit.h` | 40 | Shader edit declarations + state types |
| `shader_edit.cpp` | 250 | Build/replace/restore + state management |
| `mesh.h` | 30 | Mesh export declarations |
| `mesh.cpp` | 200 | GetPostVSData + buffer decode + OBJ generation |
| `snapshot.h` | 20 | Snapshot declaration |
| `snapshot.cpp` | 150 | Aggregated export orchestration |
| `usage.h` | 20 | Resource usage declarations |
| `usage.cpp` | 80 | GetUsage wrapper |
| `assertions.h` | 40 | Assertion result types + declarations |
| `assertions.cpp` | 250 | 5 assertion implementations |

### MCP Layer (`src/mcp/`)

| File | Purpose |
|------|---------|
| `tools/shader_edit_tools.cpp` | 5 shader edit tool registrations |
| `tools/mesh_tools.cpp` | export_mesh registration |
| `tools/snapshot_tools.cpp` | export_snapshot registration |
| `tools/usage_tools.cpp` | get_resource_usage registration |
| `tools/assertion_tools.cpp` | 5 assertion tool registrations |
| `serialization.cpp` (update) | to_json for new types |

### CLI Layer (`src/cli/main.cpp` update)

New commands: `shader-encodings`, `shader-build`, `shader-replace`, `shader-restore`, `shader-restore-all`, `mesh`, `snapshot`, `usage`, `assert-pixel`, `assert-state`, `assert-image`, `assert-count`, `assert-clean`

### Tests (`tests/`)

| File | Type | Coverage |
|------|------|---------|
| `test_serialization.cpp` (update) | Unit | New type serialization |
| `test_tools_phase2.cpp` | Integration | All 13 new tools |
| `test_assertions.cpp` | Integration | Assertion-specific edge cases |
| `test_protocol.cpp` (update) | Protocol | Tool count 27 → 40 |

### Dependencies

| Dependency | Purpose | Integration |
|-----------|---------|-------------|
| `stb_image.h` | PNG reading for assert_image | header-only in third_party/ |

---

## 5. Error Handling

### New Error Codes (`errors.h`)

| Code | Name | Context |
|------|------|---------|
| `BuildFailed` | Shader compilation failed | shader_build |
| `UnknownShaderId` | shader_id not in built_shaders | shader_replace |
| `NoReplacementActive` | No active replacement for stage | shader_restore |
| `UnknownEncoding` | Encoding name not recognized | shader_build |
| `NoShaderBound` | No shader bound at stage for event | shader_replace/restore |
| `MeshNotAvailable` | No post-VS data (not a draw call) | export_mesh |
| `ImageSizeMismatch` | Two images have different dimensions | assert_image |
| `ImageLoadFailed` | Cannot load PNG file | assert_image |
| `InvalidPath` | State path navigation failed | assert_state |

### Error Propagation

- Core functions throw `RdcError` with code + message
- MCP layer catches and returns JSON-RPC error response
- CLI layer catches and prints to stderr with exit code
- Assertions do NOT throw for pass/fail — only for system errors (no session, invalid params)

---

## 6. Scope Exclusions

The following are explicitly NOT in Phase 2:
- Frame diff (comparing two captures) — separate future phase
- VFS virtual filesystem — separate future phase
- Remote replay / Android support — separate future phase
- GPU performance counters — separate future phase
- Capture file introspection (sections, callstacks, gpus, thumbnail) — separate future phase
