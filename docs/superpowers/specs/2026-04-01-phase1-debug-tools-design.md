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

### Pixel Query Types

```cpp
// Reusable RGBA color
struct PixelColor {
    float r = 0, g = 0, b = 0, a = 0;
};

// A single modification to a pixel by a draw call
struct PixelModification {
    uint32_t eventId = 0;
    uint32_t fragmentIndex = 0;
    uint32_t primitiveId = 0;
    PixelColor shaderOut;                   // shader output color
    PixelColor postMod;                     // color after blending
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
    PixelColor color;
};
```

### Shader Debug Types

```cpp
// A variable change during shader execution
struct DebugVariable {
    std::string name;
    std::string type;       // "float", "int", "uint"
    uint32_t rows = 0;
    uint32_t cols = 0;
    std::vector<float> before;  // flat value list
    std::vector<float> after;   // flat value list
};

// A single step in the shader execution trace
struct DebugStep {
    uint32_t step = 0;
    uint32_t instruction = 0;
    std::string file;           // source file name, or ""
    int32_t line = -1;          // source line, or -1
    std::vector<DebugVariable> changes;
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
    PixelColor minVal;
    PixelColor maxVal;
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

// Query full pixel modification history across the frame
PixelHistoryResult pixelHistory(
    const Session& session,
    uint32_t x, uint32_t y,
    uint32_t targetIndex = 0,
    uint32_t sample = 0,
    std::optional<uint32_t> eventId = std::nullopt);

// Read single pixel RGBA at current/specified event
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

**Shared validation logic (both functions):**
1. Get controller from session (throws NoCaptureOpen)
2. Set frame event if eventId specified
3. Get pipeline state -> extract output render targets
4. Validate targetIndex is in range and non-null
5. Validate (x, y) within texture dimensions
6. Reject MSAA textures (msSamp > 1) with error
7. Create Subresource with sample index

### debug.h

```cpp
namespace renderdoc::core {

ShaderDebugResult debugPixel(
    const Session& session,
    uint32_t eventId,
    uint32_t x, uint32_t y,
    bool fullTrace = false,
    uint32_t sample = 0xFFFFFFFF,
    uint32_t primitive = 0xFFFFFFFF);

ShaderDebugResult debugVertex(
    const Session& session,
    uint32_t eventId,
    uint32_t vertexId,
    bool fullTrace = false,
    uint32_t instance = 0);

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
- `debugVertex`: `controller->DebugVertex(vtxId, instance, idx, view)` -> same debug loop
- `debugThread`: `controller->DebugThread({gx,gy,gz}, {tx,ty,tz})` -> same debug loop

**Debug loop pattern (shared by all three):**
1. Navigate to event, call the appropriate Debug* API
2. Check trace.debugger is valid (throw NoFragmentFound if null)
3. Loop: call `ContinueDebug(debugger)` until states list is empty
4. Collect changes at each step (if fullTrace=true)
5. Extract inputs from first step, outputs from last step
6. Always call `FreeTrace` in cleanup (RAII or try/finally equivalent)
7. Max 50,000 steps to prevent runaway traces

**Validation:**
- `debugPixel`: validate event exists, x/y >= 0
- `debugVertex`: validate event exists
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
- `controller->GetMinMax(resourceId, subresource, compType)` -> returns (minVal, maxVal)
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
  "description": "Query the full modification history of a pixel across all draw calls in the frame. Returns which draws wrote to the pixel, shader output colors, post-blend colors, depth values, and pass/fail status.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "x":           {"type": "integer", "description": "Pixel X coordinate"},
      "y":           {"type": "integer", "description": "Pixel Y coordinate"},
      "targetIndex": {"type": "integer", "description": "Color render target index (0-7), default 0"},
      "sample":      {"type": "integer", "description": "MSAA sample index, default 0"},
      "eventId":     {"type": "integer", "description": "Event ID to query at (default: current)"}
    },
    "required": ["x", "y"]
  }
}
```

#### `pick_pixel`

```json
{
  "name": "pick_pixel",
  "description": "Read the RGBA color of a single pixel at the current or specified event.",
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
  "description": "Debug the pixel/fragment shader at a specific pixel. Returns shader inputs, outputs, and optionally a full step-by-step execution trace with variable changes.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "eventId":   {"type": "integer", "description": "Draw call event ID"},
      "x":         {"type": "integer", "description": "Pixel X coordinate"},
      "y":         {"type": "integer", "description": "Pixel Y coordinate"},
      "mode":      {"type": "string", "enum": ["summary", "trace"], "description": "summary=inputs/outputs only (default), trace=full step-by-step execution"},
      "sample":    {"type": "integer", "description": "MSAA sample index (default: any)"},
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
      "instance": {"type": "integer", "description": "Instance index, default 0"}
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
  "description": "Get min/max color values and optionally a 256-bucket histogram for a texture. Useful for detecting NaN values, all-black textures, or unexpected value ranges.",
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
| `pixel` | `pixel X Y [-e EID] [--target N] [--sample N]` | Print pixel modification history |
| `pick-pixel` | `pick-pixel X Y [-e EID] [--target N]` | Print pixel RGBA color |
| `debug` | `debug pixel X Y -e EID [--trace] [--sample N] [--primitive N]` | Debug pixel shader |
| `debug` | `debug vertex VTX_ID -e EID [--trace] [--instance N]` | Debug vertex shader |
| `debug` | `debug thread GX GY GZ TX TY TZ -e EID [--trace]` | Debug compute thread |
| `tex-stats` | `tex-stats RESOURCE_ID [-e EID] [--mip N] [--slice N] [--histogram]` | Print texture min/max/histogram |

Note: `debug` is a single CLI command with a subcommand (`pixel`/`vertex`/`thread`) as its first positional argument.

## Error Handling

New error codes added to `CoreError::Code`:

| Code | When |
|------|------|
| `InvalidCoordinates` | (x,y) out of texture bounds |
| `NoFragmentFound` | No debuggable fragment at pixel / trace.debugger is null |
| `DebugNotSupported` | Event is not a draw (for debug_pixel/vertex) or not a dispatch (for debug_thread) |
| `TargetNotFound` | Specified color target index is invalid or null |

These map to JSON-RPC error codes:
- `InvalidCoordinates` -> -32602 (Invalid params)
- `NoFragmentFound` -> -32001 (Application error)
- `DebugNotSupported` -> -32001 (Application error)
- `TargetNotFound` -> -32001 (Application error)

## Serialization

New `to_json()` overloads in `serialization.h/cpp`:

```cpp
nlohmann::json to_json(const core::PixelColor& c);
nlohmann::json to_json(const core::PixelModification& m);
nlohmann::json to_json(const core::PixelHistoryResult& r);
nlohmann::json to_json(const core::PickPixelResult& r);
nlohmann::json to_json(const core::DebugVariable& v);
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
