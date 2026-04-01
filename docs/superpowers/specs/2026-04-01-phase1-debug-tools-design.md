# Phase 1: Core Debug Tools Design Spec

**Date:** 2026-04-01
**Scope:** 6 new tools — pixel_history, pick_pixel, debug_pixel, debug_vertex, debug_thread, get_texture_stats

## Overview

Phase 1 adds pixel-level inspection, shader debugging, and texture statistics to renderdoc-mcp. These are the most critical capabilities for AI-driven rendering diagnosis — without them, many bugs can only be guessed at, not confirmed.

All 6 features follow the existing layered architecture: core module -> MCP tool + CLI command.

## Architecture

Three new core modules, three new MCP tool registration files, and corresponding CLI commands:

```
src/core/pixel.h/cpp       -> src/mcp/tools/pixel_tools.cpp
src/core/debug.h/cpp       -> src/mcp/tools/debug_tools.cpp
src/core/texstats.h/cpp    -> src/mcp/tools/texstats_tools.cpp
```

CLI commands are added to `src/cli/main.cpp` following the existing `command [--option VALUE]` style.

## Type Definitions (types.h)

### Pixel Value Type

RenderDoc's `PixelValue` is a union of `float[4]`, `uint32_t[4]`, `int32_t[4]`. We preserve all three
representations and let the consumer choose based on the render target format.

```cpp
// Typed pixel value — preserves float/uint/int representations from RenderDoc's PixelValue
struct PixelValue {
    float floatValue[4] = {};
    uint32_t uintValue[4] = {};
    int32_t intValue[4] = {};
};
```

### Pixel Query Types

```cpp
// A single modification to a pixel by a draw call
struct PixelModification {
    uint32_t eventId = 0;
    uint32_t fragmentIndex = 0;
    uint32_t primitiveId = 0;
    PixelValue shaderOut;                   // shader output value
    PixelValue postMod;                     // value after blending
    std::optional<float> depth;             // None for sentinel/-1.0/non-finite
    bool passed = false;                    // depth/stencil test result
    std::vector<std::string> flags;         // "directShaderWrite", "unboundPS", etc.
};

// Result of pixel_history query
struct PixelHistoryResult {
    uint32_t x = 0, y = 0, eventId = 0;
    uint32_t targetIndex = 0;
    ResourceId targetId = 0;
    std::vector<PixelModification> modifications;
};

// Result of pick_pixel query
struct PickPixelResult {
    uint32_t x = 0, y = 0, eventId = 0;
    uint32_t targetIndex = 0;
    ResourceId targetId = 0;
    PixelValue color;
};
```

### Shader Debug Types

RenderDoc's `ShaderVariable` carries a `VarType` enum, `ShaderVariableFlags`, nested `members`,
and a `ShaderValue` union. We preserve type fidelity by mirroring these fields.

```cpp
// Mirrors RenderDoc's ShaderVariable with full type info
struct DebugVariable {
    std::string name;
    std::string type;       // VarType as string: "Float", "UInt", "SInt", "Bool", etc.
    uint32_t rows = 0;
    uint32_t cols = 0;
    uint32_t flags = 0;     // ShaderVariableFlags bitmask

    // Value union — populated based on type
    std::vector<float> floatValues;     // for Float types
    std::vector<uint32_t> uintValues;   // for UInt types
    std::vector<int32_t> intValues;     // for SInt types

    // Nested struct/array members (recursive)
    std::vector<DebugVariable> members;
};

// A single step in the shader execution trace
struct DebugStep {
    uint32_t step = 0;
    uint32_t instruction = 0;
    std::string file;           // source file name, or ""
    int32_t line = -1;          // source line, or -1
    std::vector<DebugVariableChange> changes;
};

// Before/after pair for a variable change (mirrors ShaderVariableChange)
struct DebugVariableChange {
    DebugVariable before;
    DebugVariable after;
};

// Full shader debug result
struct ShaderDebugResult {
    uint32_t eventId = 0;
    std::string stage;          // "vs", "hs", "ds", "gs", "ps", "cs"
    uint32_t totalSteps = 0;
    std::vector<DebugVariable> inputs;
    std::vector<DebugVariable> outputs;
    std::vector<DebugStep> trace;   // populated only when mode="trace"
};
```

### Texture Stats Types

```cpp
struct TextureStats {
    ResourceId id = 0;
    uint32_t eventId = 0;
    uint32_t mip = 0;
    uint32_t slice = 0;
    PixelValue minVal;      // uses typed PixelValue, not float-only
    PixelValue maxVal;
    struct HistogramBucket {
        uint32_t r = 0, g = 0, b = 0, a = 0;
    };
    std::vector<HistogramBucket> histogram;  // 256 buckets when histogram=true
};
```

## Core API

### pixel.h

```cpp
namespace renderdoc::core {

// Query pixel modification history up to the current/specified event.
//
// IMPORTANT: RenderDoc's PixelHistory filters events with eventId <= m_EventID
// (see replay_controller.cpp:1498). The result only contains modifications up to
// the specified event, NOT the entire frame. To get full-frame history, navigate
// to the last event first or pass the last event ID.
PixelHistoryResult pixelHistory(
    const Session& session,
    uint32_t x, uint32_t y,
    uint32_t targetIndex = 0,
    std::optional<uint32_t> eventId = std::nullopt);

// Read single pixel value at current/specified event
PickPixelResult pickPixel(
    const Session& session,
    uint32_t x, uint32_t y,
    uint32_t targetIndex = 0,
    std::optional<uint32_t> eventId = std::nullopt);

} // namespace renderdoc::core
```

**RenderDoc API calls:**
- `pixelHistory`: `controller->PixelHistory(rtResourceId, x, y, subresource, compType)`
- `pickPixel`: `controller->PickPixel(rtResourceId, x, y, subresource, compType)`

**MSAA policy for Phase 1:**
Phase 1 does NOT support MSAA targets. Both functions reject textures with `msSamp > 1`
by throwing `CoreError(TargetNotFound, "MSAA targets not supported in Phase 1")`.
The `sample` parameter is removed from the public API — it will be added in a future phase
when MSAA support is properly designed and tested. Internally, `Subresource.sample` is
always set to 0.

**Shared validation logic (both functions):**
1. Get controller from session (throws NoCaptureOpen)
2. Set frame event if eventId specified
3. Get pipeline state -> extract output render targets
4. Validate targetIndex is in range and non-null
5. Reject MSAA textures (msSamp > 1) with error
6. Validate (x, y) within texture dimensions
7. Create Subresource with sample=0

### debug.h

```cpp
namespace renderdoc::core {

ShaderDebugResult debugPixel(
    const Session& session,
    uint32_t eventId,
    uint32_t x, uint32_t y,
    bool fullTrace = false,
    uint32_t primitive = 0xFFFFFFFF);

ShaderDebugResult debugVertex(
    const Session& session,
    uint32_t eventId,
    uint32_t vertexId,
    bool fullTrace = false,
    uint32_t instance = 0,
    uint32_t index = 0xFFFFFFFF,
    uint32_t view = 0);

ShaderDebugResult debugThread(
    const Session& session,
    uint32_t eventId,
    uint32_t groupX, uint32_t groupY, uint32_t groupZ,
    uint32_t threadX, uint32_t threadY, uint32_t threadZ,
    bool fullTrace = false);

} // namespace renderdoc::core
```

**RenderDoc API calls:**
- `debugPixel`: `controller->DebugPixel(x, y, debugPixelInputs)` -> `controller->ContinueDebug(trace.debugger)` loop -> `controller->FreeTrace(trace)`
- `debugVertex`: `controller->DebugVertex(vertid, instid, idx, view)` -> same debug loop
- `debugThread`: `controller->DebugThread({gx,gy,gz}, {tx,ty,tz})` -> same debug loop

**`debugVertex` parameter semantics:**
The RenderDoc API `DebugVertex(vertid, instid, idx, view)` requires 4 parameters:
- `vertexId`: The vertex index to debug
- `instance`: Instance index (default 0)
- `index`: The raw index buffer value. For indexed draws, this is the index buffer entry that
  maps to the vertex. Default `0xFFFFFFFF` means "same as vertexId" — the implementation
  passes `vertexId` when `index == 0xFFFFFFFF`.
- `view`: Multiview view index (default 0). Only relevant for multiview rendering.

The MCP tool exposes `index` and `view` as optional parameters with sensible defaults so
that simple non-indexed, non-multiview cases work without extra arguments.

**Debug loop pattern (shared by all three):**
1. Navigate to event, call the appropriate Debug* API
2. Check trace.debugger is valid (throw NoFragmentFound if null)
3. Loop: call `ContinueDebug(debugger)` until states list is empty
4. Collect ShaderVariableChange at each step (if fullTrace=true), preserving VarType and members
5. Extract inputs from first step, outputs from last step
6. Always call `FreeTrace` in cleanup (RAII or try/finally equivalent)
7. Max 50,000 steps to prevent runaway traces

**Validation:**
- `debugPixel`: validate event exists, x/y >= 0, event is a draw call
- `debugVertex`: validate event exists, event is a draw call
- `debugThread`: validate event is a Dispatch (check ActionFlags)

### texstats.h

```cpp
namespace renderdoc::core {

TextureStats getTextureStats(
    const Session& session,
    ResourceId resourceId,
    uint32_t mip = 0,
    uint32_t slice = 0,
    bool histogram = false,
    std::optional<uint32_t> eventId = std::nullopt);

} // namespace renderdoc::core
```

**RenderDoc API calls:**
- `controller->GetMinMax(resourceId, subresource, compType)` -> returns `rdcpair<PixelValue, PixelValue>`
- If histogram=true: `controller->GetHistogram(resourceId, subresource, compType, min, max, channelMask)` x4 channels

**Validation:**
1. Resolve resourceId to texture description
2. Reject MSAA (msSamp > 1)
3. Validate mip in [0, texture.mips)
4. Validate slice in [0, texture.arraysize)

## MCP Tools

### pixel_tools.cpp — registerPixelTools()

#### `pixel_history`

```json
{
  "name": "pixel_history",
  "description": "Query the modification history of a pixel up to the current or specified event. Returns which draws wrote to the pixel, shader output values (float/uint/int), post-blend values, depth, and pass/fail status. Note: history is bounded by the specified eventId — to get full-frame history, pass the last event ID.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "x":           {"type": "integer", "description": "Pixel X coordinate"},
      "y":           {"type": "integer", "description": "Pixel Y coordinate"},
      "targetIndex": {"type": "integer", "description": "Color render target index (0-7), default 0"},
      "eventId":     {"type": "integer", "description": "Event ID to query up to (default: current event)"}
    },
    "required": ["x", "y"]
  }
}
```

#### `pick_pixel`

```json
{
  "name": "pick_pixel",
  "description": "Read the color value of a single pixel at the current or specified event. Returns float, uint, and int representations.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "x":           {"type": "integer", "description": "Pixel X coordinate"},
      "y":           {"type": "integer", "description": "Pixel Y coordinate"},
      "targetIndex": {"type": "integer", "description": "Color render target index (0-7), default 0"},
      "eventId":     {"type": "integer", "description": "Event ID (default: current)"}
    },
    "required": ["x", "y"]
  }
}
```

### debug_tools.cpp — registerDebugTools()

#### `debug_pixel`

```json
{
  "name": "debug_pixel",
  "description": "Debug the pixel/fragment shader at a specific pixel. Returns shader inputs, outputs, and optionally a full step-by-step execution trace with variable changes. Variables preserve their original types (float/uint/int) and struct members.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "eventId":   {"type": "integer", "description": "Draw call event ID"},
      "x":         {"type": "integer", "description": "Pixel X coordinate"},
      "y":         {"type": "integer", "description": "Pixel Y coordinate"},
      "mode":      {"type": "string", "enum": ["summary", "trace"], "description": "summary=inputs/outputs only (default), trace=full step-by-step execution"},
      "primitive": {"type": "integer", "description": "Primitive ID (default: any)"}
    },
    "required": ["eventId", "x", "y"]
  }
}
```

#### `debug_vertex`

```json
{
  "name": "debug_vertex",
  "description": "Debug the vertex shader for a specific vertex. Returns shader inputs, outputs, and optionally a full execution trace.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "eventId":  {"type": "integer", "description": "Draw call event ID"},
      "vertexId": {"type": "integer", "description": "Vertex index to debug"},
      "mode":     {"type": "string", "enum": ["summary", "trace"], "description": "summary (default) or trace"},
      "instance": {"type": "integer", "description": "Instance index, default 0"},
      "index":    {"type": "integer", "description": "Raw index buffer value for indexed draws. Default: same as vertexId"},
      "view":     {"type": "integer", "description": "Multiview view index, default 0"}
    },
    "required": ["eventId", "vertexId"]
  }
}
```

#### `debug_thread`

```json
{
  "name": "debug_thread",
  "description": "Debug a compute shader thread at a specific workgroup and thread coordinate. Returns shader inputs, outputs, and optionally a full execution trace.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "eventId": {"type": "integer", "description": "Dispatch event ID"},
      "groupX":  {"type": "integer", "description": "Workgroup X"},
      "groupY":  {"type": "integer", "description": "Workgroup Y"},
      "groupZ":  {"type": "integer", "description": "Workgroup Z"},
      "threadX": {"type": "integer", "description": "Thread X within workgroup"},
      "threadY": {"type": "integer", "description": "Thread Y within workgroup"},
      "threadZ": {"type": "integer", "description": "Thread Z within workgroup"},
      "mode":    {"type": "string", "enum": ["summary", "trace"], "description": "summary (default) or trace"}
    },
    "required": ["eventId", "groupX", "groupY", "groupZ", "threadX", "threadY", "threadZ"]
  }
}
```

### texstats_tools.cpp — registerTexStatsTools()

#### `get_texture_stats`

```json
{
  "name": "get_texture_stats",
  "description": "Get min/max pixel values and optionally a 256-bucket histogram for a texture. Returns typed values (float/uint/int). Useful for detecting NaN values, all-black textures, or unexpected value ranges.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "resourceId": {"type": "string", "description": "Texture resource ID (e.g. ResourceId::123)"},
      "mip":        {"type": "integer", "description": "Mip level, default 0"},
      "slice":      {"type": "integer", "description": "Array slice, default 0"},
      "histogram":  {"type": "boolean", "description": "Include 256-bucket RGBA histogram, default false"},
      "eventId":    {"type": "integer", "description": "Event ID for texture state (default: current)"}
    },
    "required": ["resourceId"]
  }
}
```

## CLI Commands

All commands follow the existing `renderdoc-cli <capture.rdc> <command> [options]` style.

| Command | Usage | Description |
|---------|-------|-------------|
| `pixel` | `pixel X Y [-e EID] [--target N]` | Print pixel modification history (up to EID) |
| `pick-pixel` | `pick-pixel X Y [-e EID] [--target N]` | Print pixel color value |
| `debug` | `debug pixel X Y -e EID [--trace] [--primitive N]` | Debug pixel shader |
| `debug` | `debug vertex VTX_ID -e EID [--trace] [--instance N] [--index N] [--view N]` | Debug vertex shader |
| `debug` | `debug thread GX GY GZ TX TY TZ -e EID [--trace]` | Debug compute thread |
| `tex-stats` | `tex-stats RESOURCE_ID [-e EID] [--mip N] [--slice N] [--histogram]` | Print texture min/max/histogram |

Note: `debug` is a single CLI command with a subcommand (`pixel`/`vertex`/`thread`) as its first positional argument.

## Error Handling

### Error model

Follows the existing contract in `mcp_server.cpp`:
- **`InvalidParamsError`** (thrown by tool registry for schema violations) -> **JSON-RPC error** with code `-32602`
- **`CoreError`** (thrown by core modules) -> **successful JSON-RPC response** with `tool result isError=true` containing the error message
- **`std::exception`** (unexpected) -> **successful JSON-RPC response** with `tool result isError=true`

No changes to the MCP server error handling code are needed. New `CoreError::Code` enums are
for internal classification only — they do NOT affect the JSON-RPC wire format.

### New CoreError::Code enums

```cpp
enum class Code {
    // ... existing ...
    NoCaptureOpen,
    FileNotFound,
    InvalidEventId,
    InvalidResourceId,
    ReplayInitFailed,
    ExportFailed,
    InternalError,
    // Phase 1 additions:
    InvalidCoordinates,  // (x,y) out of texture bounds
    NoFragmentFound,     // no debuggable fragment at pixel / trace.debugger is null
    DebugNotSupported,   // event is not a draw (for debug_pixel/vertex) or not a dispatch (for debug_thread)
    TargetNotFound,      // specified color target index is invalid or null
};
```

All new codes follow the same pattern: thrown as `CoreError`, caught by `mcp_server.cpp:169`,
returned as `makeToolResult(e.what(), true)`.

## Serialization

### PixelValue JSON format

```json
{
  "floatValue": [0.5, 0.3, 0.1, 1.0],
  "uintValue": [1056964608, ...],
  "intValue": [1056964608, ...]
}
```

All three representations are always included. The consumer picks the one matching the RT format.

### DebugVariable JSON format

```json
{
  "name": "color",
  "type": "Float",
  "rows": 1,
  "cols": 4,
  "flags": 0,
  "floatValues": [0.5, 0.3, 0.1, 1.0],
  "uintValues": [],
  "intValues": [],
  "members": []
}
```

For non-Float types, the corresponding value array is populated instead. `members` is
recursively serialized for struct variables.

### New to_json() overloads

```cpp
nlohmann::json to_json(const core::PixelValue& v);
nlohmann::json to_json(const core::PixelModification& m);
nlohmann::json to_json(const core::PixelHistoryResult& r);
nlohmann::json to_json(const core::PickPixelResult& r);
nlohmann::json to_json(const core::DebugVariable& v);
nlohmann::json to_json(const core::DebugVariableChange& c);
nlohmann::json to_json(const core::DebugStep& s);
nlohmann::json to_json(const core::ShaderDebugResult& r);
nlohmann::json to_json(const core::TextureStats& s);
```

## CMake Changes

Add to `renderdoc-core` sources:
- `src/core/pixel.h`, `src/core/pixel.cpp`
- `src/core/debug.h`, `src/core/debug.cpp`
- `src/core/texstats.h`, `src/core/texstats.cpp`

Add to `renderdoc-mcp-lib` sources:
- `src/mcp/tools/pixel_tools.cpp`
- `src/mcp/tools/debug_tools.cpp`
- `src/mcp/tools/texstats_tools.cpp`

Wire new registration functions in `mcp_server_default.cpp` (or wherever tools are registered):
```cpp
tools::registerPixelTools(registry);
tools::registerDebugTools(registry);
tools::registerTexStatsTools(registry);
```

Add declarations to `src/mcp/tools/tools.h`:
```cpp
void registerPixelTools(ToolRegistry& registry);
void registerDebugTools(ToolRegistry& registry);
void registerTexStatsTools(ToolRegistry& registry);
```

## Skill Update

After implementation, update `skills/renderdoc-mcp/SKILL.md` to document the 6 new tools in the tool reference and add diagnostic workflows that use them (e.g., "Black pixel diagnosis: use pick_pixel to check color, then pixel_history to find the last draw that wrote to it, then debug_pixel to trace the shader").

## Total Tool Count After Phase 1

Current: 21 tools -> After: 27 tools

## Design Decisions Log

1. **MSAA not supported in Phase 1** — Both `pixel_history` and `pick_pixel` reject MSAA targets.
   The `sample` parameter is removed entirely to avoid a contradictory API surface. Will be added
   in a future phase with proper MSAA testing.

2. **PixelValue preserves float/uint/int union** — Not collapsed to float-only. Integer render
   targets (R32_UINT, stencil, etc.) would lose information with float-only representation.

3. **DebugVariable preserves VarType, flags, and members** — Mirrors RenderDoc's ShaderVariable
   structure. Struct/array members are recursively serialized. This ensures complex shader
   variables (structs, arrays, typed buffers) are faithfully represented.

4. **debug_vertex exposes idx and view parameters** — Required by RenderDoc's `DebugVertex(vertid,
   instid, idx, view)` API. Defaults (`idx=0xFFFFFFFF` meaning "same as vertexId", `view=0`) make
   the common non-indexed, non-multiview case simple.

5. **pixel_history is bounded by eventId** — RenderDoc's `PixelHistory` internally filters events
   with `eventId <= m_EventID`. The tool description explicitly states "up to the current/specified
   event" rather than "across the frame".

6. **Error handling follows existing MCP contract** — `CoreError` -> `tool result isError=true`,
   NOT JSON-RPC error. No changes to `mcp_server.cpp` error handling.
