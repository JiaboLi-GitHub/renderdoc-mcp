# Phase 1: Core Debug Tools Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 6 new tools (pixel_history, pick_pixel, debug_pixel, debug_vertex, debug_thread, get_texture_stats) bringing total from 21 to 27.

**Architecture:** Three new core modules (`pixel.h/cpp`, `debug.h/cpp`, `texstats.h/cpp`) following the existing layered pattern. Each module has a corresponding MCP tool registration file and CLI commands in `main.cpp`. Types added to `types.h`, serialization to `serialization.h/cpp`.

**Tech Stack:** C++17, RenderDoc replay API, nlohmann/json, CMake

**Spec:** `docs/superpowers/specs/2026-04-01-phase1-debug-tools-design.md`

---

## File Structure

### Files to Create
| File | Responsibility |
|------|---------------|
| `src/core/pixel.h` | Core API declarations: `pixelHistory()`, `pickPixel()` |
| `src/core/pixel.cpp` | Implementation: RenderDoc PixelHistory/PickPixel calls + validation |
| `src/core/debug.h` | Core API declarations: `debugPixel()`, `debugVertex()`, `debugThread()` |
| `src/core/debug.cpp` | Implementation: RenderDoc Debug* calls + trace loop |
| `src/core/texstats.h` | Core API declaration: `getTextureStats()` |
| `src/core/texstats.cpp` | Implementation: RenderDoc GetMinMax/GetHistogram calls |
| `src/mcp/tools/pixel_tools.cpp` | MCP tool registration for pixel_history, pick_pixel |
| `src/mcp/tools/debug_tools.cpp` | MCP tool registration for debug_pixel, debug_vertex, debug_thread |
| `src/mcp/tools/texstats_tools.cpp` | MCP tool registration for get_texture_stats |

### Files to Modify
| File | Lines | Change |
|------|-------|--------|
| `src/core/types.h` | after line 255 | Add PixelValue, PixelModification, PixelHistoryResult, PickPixelResult, DebugVariable, DebugVariableChange, DebugStep, ShaderDebugResult, TextureStats types |
| `src/core/errors.h` | lines 10-18 | Add 4 new error codes to Code enum |
| `src/mcp/serialization.h` | after line 43 | Add 9 new to_json declarations |
| `src/mcp/serialization.cpp` | after line 260 | Add 9 new to_json implementations |
| `src/mcp/tools/tools.h` | after line 15 | Add 3 new register* declarations |
| `src/mcp/mcp_server_default.cpp` | after line 22 | Add 3 new register* calls |
| `CMakeLists.txt` | line 37, line 94 | Add new source files to targets |
| `src/cli/main.cpp` | multiple | Add Args fields, usage text, 4 new command implementations, dispatch entries |

---

### Task 1: Types and Error Codes

**Files:**
- Modify: `src/core/types.h:255` (before closing brace)
- Modify: `src/core/errors.h:10-18`

- [ ] **Step 1: Add PixelValue and pixel query types to types.h**

Insert before line 257 (`} // namespace renderdoc::core`):

```cpp
// --- Pixel Query ---
struct PixelValue {
    float floatValue[4] = {};
    uint32_t uintValue[4] = {};
    int32_t intValue[4] = {};
};

struct PixelModification {
    uint32_t eventId = 0;
    uint32_t fragmentIndex = 0;
    uint32_t primitiveId = 0;
    PixelValue shaderOut;
    PixelValue postMod;
    std::optional<float> depth;
    bool passed = false;
    std::vector<std::string> flags;
};

struct PixelHistoryResult {
    uint32_t x = 0, y = 0, eventId = 0;
    uint32_t targetIndex = 0;
    ResourceId targetId = 0;
    std::vector<PixelModification> modifications;
};

struct PickPixelResult {
    uint32_t x = 0, y = 0, eventId = 0;
    uint32_t targetIndex = 0;
    ResourceId targetId = 0;
    PixelValue color;
};
```

- [ ] **Step 2: Add shader debug types to types.h**

Insert after the pixel types, before closing brace:

```cpp
// --- Shader Debug ---
struct DebugVariable {
    std::string name;
    std::string type;       // VarType as string: "Float", "UInt", "SInt", "Bool", etc.
    uint32_t rows = 0;
    uint32_t cols = 0;
    uint32_t flags = 0;     // ShaderVariableFlags bitmask
    std::vector<float> floatValues;
    std::vector<uint32_t> uintValues;
    std::vector<int32_t> intValues;
    std::vector<DebugVariable> members;
};

struct DebugVariableChange {
    DebugVariable before;
    DebugVariable after;
};

struct DebugStep {
    uint32_t step = 0;
    uint32_t instruction = 0;
    std::string file;
    int32_t line = -1;
    std::vector<DebugVariableChange> changes;
};

struct ShaderDebugResult {
    uint32_t eventId = 0;
    std::string stage;
    uint32_t totalSteps = 0;
    std::vector<DebugVariable> inputs;
    std::vector<DebugVariable> outputs;
    std::vector<DebugStep> trace;
};
```

- [ ] **Step 3: Add texture stats types to types.h**

Insert after debug types, before closing brace:

```cpp
// --- Texture Stats ---
struct TextureStats {
    ResourceId id = 0;
    uint32_t eventId = 0;
    uint32_t mip = 0;
    uint32_t slice = 0;
    PixelValue minVal;
    PixelValue maxVal;
    struct HistogramBucket {
        uint32_t r = 0, g = 0, b = 0, a = 0;
    };
    std::vector<HistogramBucket> histogram;
};
```

- [ ] **Step 4: Add new error codes to errors.h**

Replace the `Code` enum (lines 10-18) with:

```cpp
    enum class Code {
        NoCaptureOpen,
        FileNotFound,
        InvalidEventId,
        InvalidResourceId,
        ReplayInitFailed,
        ExportFailed,
        InternalError,
        InvalidCoordinates,
        NoFragmentFound,
        DebugNotSupported,
        TargetNotFound
    };
```

- [ ] **Step 5: Build to verify types compile**

Run: `cmake --build build --config Release --target renderdoc-core 2>&1 | head -20`
Expected: Build succeeds (types are header-only, no new .cpp yet)

- [ ] **Step 6: Commit**

```bash
git add src/core/types.h src/core/errors.h
git commit -m "feat: add Phase 1 types and error codes

Add PixelValue, PixelModification, PixelHistoryResult, PickPixelResult,
DebugVariable, DebugVariableChange, DebugStep, ShaderDebugResult,
TextureStats types. Add InvalidCoordinates, NoFragmentFound,
DebugNotSupported, TargetNotFound error codes."
```

---

### Task 2: Core pixel module (pixel_history + pick_pixel)

**Files:**
- Create: `src/core/pixel.h`
- Create: `src/core/pixel.cpp`
- Modify: `CMakeLists.txt:37`

- [ ] **Step 1: Create src/core/pixel.h**

```cpp
#pragma once

#include "core/types.h"
#include "core/session.h"
#include <optional>

namespace renderdoc::core {

PixelHistoryResult pixelHistory(
    const Session& session,
    uint32_t x, uint32_t y,
    uint32_t targetIndex = 0,
    std::optional<uint32_t> eventId = std::nullopt);

PickPixelResult pickPixel(
    const Session& session,
    uint32_t x, uint32_t y,
    uint32_t targetIndex = 0,
    std::optional<uint32_t> eventId = std::nullopt);

} // namespace renderdoc::core
```

- [ ] **Step 2: Create src/core/pixel.cpp**

```cpp
#include "core/pixel.h"
#include "core/errors.h"
#include <renderdoc_replay.h>
#include <cstring>
#include <cmath>

namespace renderdoc::core {

namespace {

ResourceId toResourceId(::ResourceId id) {
    uint64_t raw = 0;
    static_assert(sizeof(raw) == sizeof(id), "ResourceId size mismatch");
    std::memcpy(&raw, &id, sizeof(raw));
    return raw;
}

::ResourceId fromResourceId(ResourceId id) {
    ::ResourceId rid;
    std::memcpy(&rid, &id, sizeof(rid));
    return rid;
}

// Copy RenderDoc PixelValue union to our PixelValue struct
PixelValue convertPixelValue(const ::PixelValue& pv) {
    PixelValue result;
    for (int i = 0; i < 4; i++) {
        result.floatValue[i] = pv.floatValue[i];
        result.uintValue[i]  = pv.uintValue[i];
        result.intValue[i]   = pv.intValue[i];
    }
    return result;
}

// Collect boolean flags from PixelModification into string list
std::vector<std::string> collectFlags(const ::PixelModification& mod) {
    std::vector<std::string> flags;
    if (mod.directShaderWrite) flags.push_back("directShaderWrite");
    if (mod.unboundPS)         flags.push_back("unboundPS");
    if (mod.sampleMasked)      flags.push_back("sampleMasked");
    if (mod.backfaceCulled)    flags.push_back("backfaceCulled");
    if (mod.depthClipped)      flags.push_back("depthClipped");
    if (mod.depthBoundsFailed) flags.push_back("depthBoundsFailed");
    if (mod.viewClipped)       flags.push_back("viewClipped");
    if (mod.scissorClipped)    flags.push_back("scissorClipped");
    if (mod.shaderDiscarded)   flags.push_back("shaderDiscarded");
    if (mod.depthTestFailed)   flags.push_back("depthTestFailed");
    if (mod.stencilTestFailed) flags.push_back("stencilTestFailed");
    if (mod.predicationSkipped) flags.push_back("predicationSkipped");
    return flags;
}

// Safe depth: return nullopt for sentinel values and non-finite numbers
std::optional<float> safeDepth(float d) {
    if (d == -1.0f || !std::isfinite(d))
        return std::nullopt;
    return d;
}

// Shared validation: get the render target ResourceId for a given target index.
// Sets the frame event, validates target index and coordinates.
// Returns the RenderDoc ResourceId of the target texture and its dimensions.
struct TargetInfo {
    ::ResourceId rid;
    uint32_t width;
    uint32_t height;
};

// NOTE: PipeState::GetOutputTargets() returns rdcarray<Descriptor>, not BoundResource.
// Descriptor has .resource (ResourceId), .firstMip, .firstSlice, .numMips, .numSlices.
// We must use firstMip/firstSlice from the Descriptor to build the correct Subresource.
struct TargetInfo {
    ::ResourceId rid;
    uint32_t width;
    uint32_t height;
    uint32_t firstMip;
    uint32_t firstSlice;
};

TargetInfo resolveTarget(IReplayController* ctrl, uint32_t targetIndex,
                         uint32_t x, uint32_t y) {
    // Get pipeline state to find output targets
    const PipeState& pipe = ctrl->GetPipelineState();

    // GetOutputTargets() returns rdcarray<Descriptor>
    rdcarray<Descriptor> targets = pipe.GetOutputTargets();

    if (targets.empty())
        throw CoreError(CoreError::Code::TargetNotFound,
                        "No color targets bound at current event");

    if (targetIndex >= targets.size())
        throw CoreError(CoreError::Code::TargetNotFound,
                        "Target index " + std::to_string(targetIndex) +
                        " out of range (0-" + std::to_string(targets.size() - 1) + ")");

    const Descriptor& desc = targets[targetIndex];
    ::ResourceId rtId = desc.resource;
    if (rtId == ::ResourceId::Null())
        throw CoreError(CoreError::Code::TargetNotFound,
                        "Target index " + std::to_string(targetIndex) + " is null");

    // Get texture description for bounds checking
    TextureDescription tex = ctrl->GetTexture(rtId);

    if (tex.msSamp > 1)
        throw CoreError(CoreError::Code::TargetNotFound,
                        "MSAA targets not supported in Phase 1");

    // Use mip-adjusted dimensions for bounds checking
    uint32_t mipWidth  = std::max(1u, tex.width  >> desc.firstMip);
    uint32_t mipHeight = std::max(1u, tex.height >> desc.firstMip);

    if (x >= mipWidth || y >= mipHeight)
        throw CoreError(CoreError::Code::InvalidCoordinates,
                        "(" + std::to_string(x) + "," + std::to_string(y) +
                        ") out of bounds for target " +
                        std::to_string(mipWidth) + "x" + std::to_string(mipHeight));

    return {rtId, mipWidth, mipHeight, desc.firstMip, desc.firstSlice};
}

} // anonymous namespace

PixelHistoryResult pixelHistory(
    const Session& session,
    uint32_t x, uint32_t y,
    uint32_t targetIndex,
    std::optional<uint32_t> eventId) {

    auto* ctrl = session.controller();

    if (eventId.has_value())
        ctrl->SetFrameEvent(*eventId, true);

    auto target = resolveTarget(ctrl, targetIndex, x, y);

    // Use the mip/slice from the bound descriptor view, not hardcoded 0
    Subresource sub;
    sub.mip   = target.firstMip;
    sub.slice  = target.firstSlice;
    sub.sample = 0;

    rdcarray<::PixelModification> history =
        ctrl->PixelHistory(target.rid, x, y, sub, CompType::Typeless);

    // When eventId was provided, we called SetFrameEvent so use that value.
    // When not provided, no SetFrameEvent was called so session.currentEventId() is valid.
    uint32_t actualEventId = eventId.value_or(session.currentEventId());

    PixelHistoryResult result;
    result.x = x;
    result.y = y;
    result.eventId = actualEventId;
    result.targetIndex = targetIndex;
    result.targetId = toResourceId(target.rid);

    for (size_t i = 0; i < history.size(); i++) {
        const auto& mod = history[i];
        PixelModification pm;
        pm.eventId       = mod.eventId;
        pm.fragmentIndex = mod.fragIndex;
        pm.primitiveId   = mod.primitiveID;
        pm.shaderOut     = convertPixelValue(mod.shaderOut.col);
        pm.postMod       = convertPixelValue(mod.postMod.col);
        pm.depth         = safeDepth(mod.postMod.depth);
        pm.passed        = mod.Passed();
        pm.flags         = collectFlags(mod);
        result.modifications.push_back(std::move(pm));
    }

    return result;
}

PickPixelResult pickPixel(
    const Session& session,
    uint32_t x, uint32_t y,
    uint32_t targetIndex,
    std::optional<uint32_t> eventId) {

    auto* ctrl = session.controller();

    if (eventId.has_value())
        ctrl->SetFrameEvent(*eventId, true);

    auto target = resolveTarget(ctrl, targetIndex, x, y);

    // Use the mip/slice from the bound descriptor view
    Subresource sub;
    sub.mip   = target.firstMip;
    sub.slice  = target.firstSlice;
    sub.sample = 0;

    ::PixelValue pv = ctrl->PickPixel(target.rid, x, y, sub, CompType::Typeless);

    uint32_t actualEventId = eventId.value_or(session.currentEventId());

    PickPixelResult result;
    result.x = x;
    result.y = y;
    result.eventId = actualEventId;
    result.targetIndex = targetIndex;
    result.targetId = toResourceId(target.rid);
    result.color = convertPixelValue(pv);

    return result;
}

} // namespace renderdoc::core
```

- [ ] **Step 3: Add pixel.cpp to CMakeLists.txt**

In `CMakeLists.txt`, after line 37 (`src/core/renderdoc_tostr_anchor.cpp`), add:

```
        src/core/pixel.cpp
```

- [ ] **Step 4: Build to verify pixel module compiles**

Run: `cmake --build build --config Release --target renderdoc-core 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 5: Commit**

```bash
git add src/core/pixel.h src/core/pixel.cpp CMakeLists.txt
git commit -m "feat: add core pixel module (pixelHistory + pickPixel)"
```

---

### Task 3: Core debug module (debugPixel + debugVertex + debugThread)

**Files:**
- Create: `src/core/debug.h`
- Create: `src/core/debug.cpp`
- Modify: `CMakeLists.txt:38` (after pixel.cpp)

- [ ] **Step 1: Create src/core/debug.h**

```cpp
#pragma once

#include "core/types.h"
#include "core/session.h"
#include <optional>

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

- [ ] **Step 2: Create src/core/debug.cpp**

```cpp
#include "core/debug.h"
#include "core/errors.h"
#include <renderdoc_replay.h>
#include <cstring>

namespace renderdoc::core {

namespace {

static constexpr uint32_t MAX_DEBUG_STEPS = 50000;

std::string varTypeToString(VarType t) {
    switch (t) {
        case VarType::Float:             return "Float";
        case VarType::Double:            return "Double";
        case VarType::Half:              return "Half";
        case VarType::SInt:              return "SInt";
        case VarType::UInt:              return "UInt";
        case VarType::SShort:            return "SShort";
        case VarType::UShort:            return "UShort";
        case VarType::SLong:             return "SLong";
        case VarType::ULong:             return "ULong";
        case VarType::SByte:             return "SByte";
        case VarType::UByte:             return "UByte";
        case VarType::Bool:              return "Bool";
        case VarType::Enum:              return "Enum";
        case VarType::Struct:            return "Struct";
        case VarType::GPUPointer:        return "GPUPointer";
        case VarType::ConstantBlock:     return "ConstantBlock";
        case VarType::ReadOnlyResource:  return "ReadOnlyResource";
        case VarType::ReadWriteResource: return "ReadWriteResource";
        case VarType::Sampler:           return "Sampler";
        default:                         return "Unknown";
    }
}

std::string shaderStageToStr(ShaderStage s) {
    switch (s) {
        case ShaderStage::Vertex:   return "vs";
        case ShaderStage::Hull:     return "hs";
        case ShaderStage::Domain:   return "ds";
        case ShaderStage::Geometry: return "gs";
        case ShaderStage::Pixel:    return "ps";
        case ShaderStage::Compute:  return "cs";
        default:                    return "unknown";
    }
}

bool isFloatType(VarType t) {
    return t == VarType::Float || t == VarType::Double || t == VarType::Half;
}

bool isSignedIntType(VarType t) {
    return t == VarType::SInt || t == VarType::SShort || t == VarType::SLong || t == VarType::SByte;
}

// Convert a RenderDoc ShaderVariable to our DebugVariable
DebugVariable convertVariable(const ShaderVariable& sv) {
    DebugVariable dv;
    dv.name  = std::string(sv.name.c_str());
    dv.type  = varTypeToString(sv.type);
    dv.rows  = sv.rows;
    dv.cols  = sv.columns;
    dv.flags = (uint32_t)sv.flags;

    uint32_t count = sv.rows * sv.columns;
    if (count == 0) count = 1; // at least 1 element for scalar

    if (isFloatType(sv.type)) {
        dv.floatValues.resize(count);
        for (uint32_t i = 0; i < count && i < 16; i++)
            dv.floatValues[i] = sv.value.f32v[i];
    } else if (isSignedIntType(sv.type)) {
        dv.intValues.resize(count);
        for (uint32_t i = 0; i < count && i < 16; i++)
            dv.intValues[i] = sv.value.s32v[i];
    } else {
        // UInt, Bool, Enum, pointers, etc. -> store as uint
        dv.uintValues.resize(count);
        for (uint32_t i = 0; i < count && i < 16; i++)
            dv.uintValues[i] = sv.value.u32v[i];
    }

    // Recurse into members
    for (size_t i = 0; i < sv.members.size(); i++) {
        dv.members.push_back(convertVariable(sv.members[i]));
    }

    return dv;
}

DebugVariableChange convertChange(const ShaderVariableChange& svc) {
    DebugVariableChange dc;
    dc.before = convertVariable(svc.before);
    dc.after  = convertVariable(svc.after);
    return dc;
}

// Run the debug loop: ContinueDebug until no more states.
// Returns (totalSteps, inputs, outputs, trace).
struct DebugLoopResult {
    uint32_t totalSteps = 0;
    std::vector<DebugVariable> inputs;
    std::vector<DebugVariable> outputs;
    std::vector<DebugStep> trace;
};

DebugLoopResult runDebugLoop(IReplayController* ctrl, ShaderDebugTrace* dbgTrace,
                             bool fullTrace) {
    DebugLoopResult result;
    ShaderDebugger* debugger = dbgTrace->debugger;

    // Source info for mapping instruction -> file/line
    // InstructionSourceInfo has: uint32_t instruction, LineColumnInfo lineInfo
    // LineColumnInfo has: uint32_t disassemblyLine, int32_t fileIndex,
    //                     uint32_t lineStart, lineEnd, colStart, colEnd
    // fileIndex is an index into the shader's source file list (not directly
    // available on ShaderDebugTrace in C++). We store fileIndex as-is.
    const auto& instInfo = dbgTrace->instInfo;

    // ── Inputs: use ShaderDebugTrace::inputs (stable, authoritative) ──
    // dbgTrace->inputs contains the shader's input variables as ShaderVariable[].
    // These are the actual shader inputs, not derived from step changes.
    for (size_t i = 0; i < dbgTrace->inputs.size(); i++)
        result.inputs.push_back(convertVariable(dbgTrace->inputs[i]));

    // Track last step's changes for outputs
    std::vector<ShaderVariableChange> lastChanges;
    uint32_t stepCount = 0;

    while (stepCount < MAX_DEBUG_STEPS) {
        rdcarray<ShaderDebugState> states = ctrl->ContinueDebug(debugger);
        if (states.empty())
            break;

        for (size_t si = 0; si < states.size() && stepCount < MAX_DEBUG_STEPS; si++) {
            const auto& state = states[si];

            // Track last non-empty changes for output extraction
            if (!state.changes.empty()) {
                lastChanges.clear();
                for (size_t c = 0; c < state.changes.size(); c++)
                    lastChanges.push_back(state.changes[c]);
            }

            if (fullTrace) {
                DebugStep ds;
                ds.step        = stepCount;
                ds.instruction = state.nextInstruction;

                // Look up source location from instInfo
                if (state.nextInstruction < instInfo.size()) {
                    const auto& info = instInfo[state.nextInstruction];
                    ds.line = (int32_t)info.lineInfo.lineStart;
                    // fileIndex: numeric index into shader source files.
                    // We store it as the "file" field as a string index.
                    if (info.lineInfo.fileIndex >= 0)
                        ds.file = std::to_string(info.lineInfo.fileIndex);
                }

                for (size_t c = 0; c < state.changes.size(); c++)
                    ds.changes.push_back(convertChange(state.changes[c]));

                result.trace.push_back(std::move(ds));
            }

            stepCount++;
        }
    }

    result.totalSteps = stepCount;

    // ── Outputs: extract from last step's "after" values ──
    // The last step's changes represent the final variable state modifications,
    // which includes the shader output variables.
    for (const auto& lc : lastChanges)
        result.outputs.push_back(convertVariable(lc.after));

    return result;
}

} // anonymous namespace

ShaderDebugResult debugPixel(
    const Session& session,
    uint32_t eventId,
    uint32_t x, uint32_t y,
    bool fullTrace,
    uint32_t primitive) {

    auto* ctrl = session.controller();
    ctrl->SetFrameEvent(eventId, true);

    DebugPixelInputs inputs;
    inputs.sample    = ~0U;  // any
    inputs.primitive = primitive;
    inputs.view      = ~0U;  // any

    ShaderDebugTrace* trace = ctrl->DebugPixel(x, y, inputs);
    if (!trace || !trace->debugger) {
        if (trace) ctrl->FreeTrace(trace);
        throw CoreError(CoreError::Code::NoFragmentFound,
                        "No debuggable fragment at (" + std::to_string(x) +
                        "," + std::to_string(y) + ") for event " + std::to_string(eventId));
    }

    ShaderDebugResult result;
    result.eventId = eventId;
    result.stage   = shaderStageToStr(trace->stage);

    try {
        auto loopResult = runDebugLoop(ctrl, trace, fullTrace);
        result.totalSteps = loopResult.totalSteps;
        result.inputs     = std::move(loopResult.inputs);
        result.outputs    = std::move(loopResult.outputs);
        result.trace      = std::move(loopResult.trace);
    } catch (...) {
        ctrl->FreeTrace(trace);
        throw;
    }

    ctrl->FreeTrace(trace);
    return result;
}

ShaderDebugResult debugVertex(
    const Session& session,
    uint32_t eventId,
    uint32_t vertexId,
    bool fullTrace,
    uint32_t instance,
    uint32_t index,
    uint32_t view) {

    auto* ctrl = session.controller();
    ctrl->SetFrameEvent(eventId, true);

    // If index not specified, use vertexId as the raw index value
    uint32_t idx = (index == 0xFFFFFFFF) ? vertexId : index;

    ShaderDebugTrace* trace = ctrl->DebugVertex(vertexId, instance, idx, view);
    if (!trace || !trace->debugger) {
        if (trace) ctrl->FreeTrace(trace);
        throw CoreError(CoreError::Code::NoFragmentFound,
                        "Cannot debug vertex " + std::to_string(vertexId) +
                        " at event " + std::to_string(eventId));
    }

    ShaderDebugResult result;
    result.eventId = eventId;
    result.stage   = shaderStageToStr(trace->stage);

    try {
        auto loopResult = runDebugLoop(ctrl, trace, fullTrace);
        result.totalSteps = loopResult.totalSteps;
        result.inputs     = std::move(loopResult.inputs);
        result.outputs    = std::move(loopResult.outputs);
        result.trace      = std::move(loopResult.trace);
    } catch (...) {
        ctrl->FreeTrace(trace);
        throw;
    }

    ctrl->FreeTrace(trace);
    return result;
}

ShaderDebugResult debugThread(
    const Session& session,
    uint32_t eventId,
    uint32_t groupX, uint32_t groupY, uint32_t groupZ,
    uint32_t threadX, uint32_t threadY, uint32_t threadZ,
    bool fullTrace) {

    auto* ctrl = session.controller();
    ctrl->SetFrameEvent(eventId, true);

    // Validate this is a dispatch event by checking action flags
    const ActionDescription* action = ctrl->GetAction();
    if (!action || !(action->flags & ActionFlags::Dispatch))
        throw CoreError(CoreError::Code::DebugNotSupported,
                        "Event " + std::to_string(eventId) + " is not a dispatch");

    rdcfixedarray<uint32_t, 3> groupid  = {groupX, groupY, groupZ};
    rdcfixedarray<uint32_t, 3> threadid = {threadX, threadY, threadZ};

    ShaderDebugTrace* trace = ctrl->DebugThread(groupid, threadid);
    if (!trace || !trace->debugger) {
        if (trace) ctrl->FreeTrace(trace);
        throw CoreError(CoreError::Code::NoFragmentFound,
                        "Cannot debug thread (" + std::to_string(threadX) + "," +
                        std::to_string(threadY) + "," + std::to_string(threadZ) +
                        ") at event " + std::to_string(eventId));
    }

    ShaderDebugResult result;
    result.eventId = eventId;
    result.stage   = shaderStageToStr(trace->stage);

    try {
        auto loopResult = runDebugLoop(ctrl, trace, fullTrace);
        result.totalSteps = loopResult.totalSteps;
        result.inputs     = std::move(loopResult.inputs);
        result.outputs    = std::move(loopResult.outputs);
        result.trace      = std::move(loopResult.trace);
    } catch (...) {
        ctrl->FreeTrace(trace);
        throw;
    }

    ctrl->FreeTrace(trace);
    return result;
}

} // namespace renderdoc::core
```

- [ ] **Step 3: Add debug.cpp to CMakeLists.txt**

After `src/core/pixel.cpp` in the renderdoc-core sources, add:

```
        src/core/debug.cpp
```

- [ ] **Step 4: Build to verify debug module compiles**

Run: `cmake --build build --config Release --target renderdoc-core 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 5: Commit**

```bash
git add src/core/debug.h src/core/debug.cpp CMakeLists.txt
git commit -m "feat: add core debug module (debugPixel, debugVertex, debugThread)"
```

---

### Task 4: Core texstats module (getTextureStats)

**Files:**
- Create: `src/core/texstats.h`
- Create: `src/core/texstats.cpp`
- Modify: `CMakeLists.txt` (after debug.cpp)

- [ ] **Step 1: Create src/core/texstats.h**

```cpp
#pragma once

#include "core/types.h"
#include "core/session.h"
#include <optional>

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

- [ ] **Step 2: Create src/core/texstats.cpp**

```cpp
#include "core/texstats.h"
#include "core/errors.h"
#include <renderdoc_replay.h>
#include <cstring>

namespace renderdoc::core {

namespace {

::ResourceId fromResourceId(ResourceId id) {
    ::ResourceId rid;
    std::memcpy(&rid, &id, sizeof(rid));
    return rid;
}

PixelValue convertPixelValue(const ::PixelValue& pv) {
    PixelValue result;
    for (int i = 0; i < 4; i++) {
        result.floatValue[i] = pv.floatValue[i];
        result.uintValue[i]  = pv.uintValue[i];
        result.intValue[i]   = pv.intValue[i];
    }
    return result;
}

} // anonymous namespace

TextureStats getTextureStats(
    const Session& session,
    ResourceId resourceId,
    uint32_t mip,
    uint32_t slice,
    bool histogram,
    std::optional<uint32_t> eventId) {

    auto* ctrl = session.controller();

    if (eventId.has_value())
        ctrl->SetFrameEvent(*eventId, true);

    ::ResourceId rid = fromResourceId(resourceId);

    // Get texture description for validation
    TextureDescription tex = ctrl->GetTexture(rid);
    if (tex.resourceId == ::ResourceId::Null())
        throw CoreError(CoreError::Code::InvalidResourceId,
                        "Resource not found: " + std::to_string(resourceId));

    if (tex.msSamp > 1)
        throw CoreError(CoreError::Code::InvalidResourceId,
                        "MSAA textures not supported for stats");

    if (mip >= tex.mips)
        throw CoreError(CoreError::Code::InvalidResourceId,
                        "Mip " + std::to_string(mip) + " out of range (0-" +
                        std::to_string(tex.mips - 1) + ")");

    if (slice >= tex.arraysize)
        throw CoreError(CoreError::Code::InvalidResourceId,
                        "Slice " + std::to_string(slice) + " out of range (0-" +
                        std::to_string(tex.arraysize - 1) + ")");

    Subresource sub;
    sub.mip   = mip;
    sub.slice  = slice;
    sub.sample = 0;

    // Determine appropriate CompType from texture format.
    // For integer formats, we must use UInt/SInt so GetMinMax returns data
    // in the correct union branch, and histogram ranges are meaningful.
    CompType compType = CompType::Typeless;
    if (tex.format.compType == CompType::UInt)
        compType = CompType::UInt;
    else if (tex.format.compType == CompType::SInt)
        compType = CompType::SInt;

    auto minmax = ctrl->GetMinMax(rid, sub, compType);

    uint32_t actualEventId = eventId.value_or(session.currentEventId());

    TextureStats result;
    result.id      = resourceId;
    result.eventId = actualEventId;
    result.mip     = mip;
    result.slice   = slice;
    result.minVal  = convertPixelValue(minmax.first);
    result.maxVal  = convertPixelValue(minmax.second);

    if (histogram) {
        // Pre-fill 256 buckets
        result.histogram.resize(256);

        // For histogram range: use float values for float textures,
        // cast int/uint to float for range computation on integer textures.
        auto getRange = [&](int ch) -> std::pair<float, float> {
            float minF, maxF;
            if (compType == CompType::UInt) {
                minF = static_cast<float>(minmax.first.uintValue[ch]);
                maxF = static_cast<float>(minmax.second.uintValue[ch]);
            } else if (compType == CompType::SInt) {
                minF = static_cast<float>(minmax.first.intValue[ch]);
                maxF = static_cast<float>(minmax.second.intValue[ch]);
            } else {
                minF = minmax.first.floatValue[ch];
                maxF = minmax.second.floatValue[ch];
            }
            if (minF == maxF) maxF = minF + 1.0f;
            return {minF, maxF};
        };

        // Query histogram per channel (R, G, B, A)
        for (int ch = 0; ch < 4; ch++) {
            rdcfixedarray<bool, 4> channels = {ch == 0, ch == 1, ch == 2, ch == 3};
            auto [minF, maxF] = getRange(ch);

            rdcarray<uint32_t> buckets =
                ctrl->GetHistogram(rid, sub, compType, minF, maxF, channels);

            for (size_t b = 0; b < buckets.size() && b < 256; b++) {
                switch (ch) {
                    case 0: result.histogram[b].r = buckets[b]; break;
                    case 1: result.histogram[b].g = buckets[b]; break;
                    case 2: result.histogram[b].b = buckets[b]; break;
                    case 3: result.histogram[b].a = buckets[b]; break;
                }
            }
        }
    }

    return result;
}

} // namespace renderdoc::core
```

- [ ] **Step 3: Add texstats.cpp to CMakeLists.txt**

After `src/core/debug.cpp` in the renderdoc-core sources, add:

```
        src/core/texstats.cpp
```

- [ ] **Step 4: Build to verify texstats module compiles**

Run: `cmake --build build --config Release --target renderdoc-core 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 5: Commit**

```bash
git add src/core/texstats.h src/core/texstats.cpp CMakeLists.txt
git commit -m "feat: add core texstats module (getTextureStats)"
```

---

### Task 5: Serialization

**Files:**
- Modify: `src/mcp/serialization.h:43` (after last declaration)
- Modify: `src/mcp/serialization.cpp:260` (after last implementation)

- [ ] **Step 1: Add new to_json declarations to serialization.h**

Insert after line 43 (`nlohmann::json to_json(const core::RenderTargetInfo& rt);`):

```cpp
nlohmann::json to_json(const core::PixelValue& val);
nlohmann::json to_json(const core::PixelModification& mod);
nlohmann::json to_json(const core::PixelHistoryResult& result);
nlohmann::json to_json(const core::PickPixelResult& result);
nlohmann::json to_json(const core::DebugVariable& var);
nlohmann::json to_json(const core::DebugVariableChange& change);
nlohmann::json to_json(const core::DebugStep& step);
nlohmann::json to_json(const core::ShaderDebugResult& result);
nlohmann::json to_json(const core::TextureStats& stats);
```

- [ ] **Step 2: Add to_json implementations to serialization.cpp**

Append after line 260 (after the `to_json(CaptureResult)` function), before the closing `}` of the namespace:

```cpp
// --- Phase 1: Pixel, Debug, TexStats serialization ---

nlohmann::json to_json(const core::PixelValue& val) {
    return {
        {"floatValue", {val.floatValue[0], val.floatValue[1], val.floatValue[2], val.floatValue[3]}},
        {"uintValue",  {val.uintValue[0],  val.uintValue[1],  val.uintValue[2],  val.uintValue[3]}},
        {"intValue",   {val.intValue[0],   val.intValue[1],   val.intValue[2],   val.intValue[3]}}
    };
}

nlohmann::json to_json(const core::PixelModification& mod) {
    nlohmann::json j;
    j["eventId"]       = mod.eventId;
    j["fragmentIndex"] = mod.fragmentIndex;
    j["primitiveId"]   = mod.primitiveId;
    j["shaderOut"]     = to_json(mod.shaderOut);
    j["postMod"]       = to_json(mod.postMod);
    if (mod.depth.has_value())
        j["depth"] = *mod.depth;
    else
        j["depth"] = nullptr;
    j["passed"] = mod.passed;
    j["flags"]  = mod.flags;
    return j;
}

nlohmann::json to_json(const core::PixelHistoryResult& result) {
    nlohmann::json j;
    j["x"]           = result.x;
    j["y"]           = result.y;
    j["eventId"]     = result.eventId;
    j["targetIndex"] = result.targetIndex;
    j["targetId"]    = resourceIdToString(result.targetId);
    j["modifications"] = to_json_array(result.modifications);
    return j;
}

nlohmann::json to_json(const core::PickPixelResult& result) {
    return {
        {"x",           result.x},
        {"y",           result.y},
        {"eventId",     result.eventId},
        {"targetIndex", result.targetIndex},
        {"targetId",    resourceIdToString(result.targetId)},
        {"color",       to_json(result.color)}
    };
}

nlohmann::json to_json(const core::DebugVariable& var) {
    nlohmann::json j;
    j["name"]  = var.name;
    j["type"]  = var.type;
    j["rows"]  = var.rows;
    j["cols"]  = var.cols;
    j["flags"] = var.flags;

    if (!var.floatValues.empty()) j["floatValues"] = var.floatValues;
    else                         j["floatValues"] = nlohmann::json::array();

    if (!var.uintValues.empty())  j["uintValues"] = var.uintValues;
    else                          j["uintValues"] = nlohmann::json::array();

    if (!var.intValues.empty())   j["intValues"] = var.intValues;
    else                          j["intValues"] = nlohmann::json::array();

    if (!var.members.empty())     j["members"] = to_json_array(var.members);
    else                          j["members"] = nlohmann::json::array();

    return j;
}

nlohmann::json to_json(const core::DebugVariableChange& change) {
    return {
        {"before", to_json(change.before)},
        {"after",  to_json(change.after)}
    };
}

nlohmann::json to_json(const core::DebugStep& step) {
    nlohmann::json j;
    j["step"]        = step.step;
    j["instruction"] = step.instruction;
    j["file"]        = step.file;
    j["line"]        = step.line;
    j["changes"]     = to_json_array(step.changes);
    return j;
}

nlohmann::json to_json(const core::ShaderDebugResult& result) {
    nlohmann::json j;
    j["eventId"]    = result.eventId;
    j["stage"]      = result.stage;
    j["totalSteps"] = result.totalSteps;
    j["inputs"]     = to_json_array(result.inputs);
    j["outputs"]    = to_json_array(result.outputs);
    if (!result.trace.empty())
        j["trace"] = to_json_array(result.trace);
    return j;
}

nlohmann::json to_json(const core::TextureStats& stats) {
    nlohmann::json j;
    j["id"]      = resourceIdToString(stats.id);
    j["eventId"] = stats.eventId;
    j["mip"]     = stats.mip;
    j["slice"]   = stats.slice;
    j["min"]     = to_json(stats.minVal);
    j["max"]     = to_json(stats.maxVal);
    if (!stats.histogram.empty()) {
        auto arr = nlohmann::json::array();
        for (const auto& b : stats.histogram) {
            arr.push_back({{"r", b.r}, {"g", b.g}, {"b", b.b}, {"a", b.a}});
        }
        j["histogram"] = arr;
    }
    return j;
}
```

- [ ] **Step 3: Build to verify serialization compiles**

Run: `cmake --build build --config Release --target renderdoc-mcp-proto 2>&1 | tail -5`
Expected: Build succeeds (serialization is in the proto target)

- [ ] **Step 4: Commit**

```bash
git add src/mcp/serialization.h src/mcp/serialization.cpp
git commit -m "feat: add serialization for Phase 1 types"
```

---

### Task 6: MCP tool registration (pixel_tools)

**Files:**
- Create: `src/mcp/tools/pixel_tools.cpp`
- Modify: `src/mcp/tools/tools.h:15`
- Modify: `src/mcp/mcp_server_default.cpp:22`
- Modify: `CMakeLists.txt:94`

- [ ] **Step 1: Create src/mcp/tools/pixel_tools.cpp**

```cpp
#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/pixel.h"

namespace renderdoc::mcp::tools {

void registerPixelTools(ToolRegistry& registry) {

    // ── pixel_history ────────────────────────────────────────────────────────
    registry.registerTool({
        "pixel_history",
        "Query the modification history of a pixel up to the current or specified event. "
        "Returns which draws wrote to the pixel, shader output values (float/uint/int), "
        "post-blend values, depth, and pass/fail status. "
        "Note: history is bounded by the specified eventId.",
        {{"type", "object"},
         {"properties", {
             {"x",           {{"type", "integer"}, {"description", "Pixel X coordinate"}}},
             {"y",           {{"type", "integer"}, {"description", "Pixel Y coordinate"}}},
             {"targetIndex", {{"type", "integer"}, {"description", "Color render target index (0-7), default 0"}}},
             {"eventId",     {{"type", "integer"}, {"description", "Event ID to query up to (default: current event)"}}}
         }},
         {"required", nlohmann::json::array({"x", "y"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            uint32_t x = args["x"].get<uint32_t>();
            uint32_t y = args["y"].get<uint32_t>();
            uint32_t targetIndex = args.value("targetIndex", 0u);

            std::optional<uint32_t> eventId;
            if (args.contains("eventId"))
                eventId = args["eventId"].get<uint32_t>();

            auto result = core::pixelHistory(session, x, y, targetIndex, eventId);
            return to_json(result);
        }
    });

    // ── pick_pixel ───────────────────────────────────────────────────────────
    registry.registerTool({
        "pick_pixel",
        "Read the color value of a single pixel at the current or specified event. "
        "Returns float, uint, and int representations.",
        {{"type", "object"},
         {"properties", {
             {"x",           {{"type", "integer"}, {"description", "Pixel X coordinate"}}},
             {"y",           {{"type", "integer"}, {"description", "Pixel Y coordinate"}}},
             {"targetIndex", {{"type", "integer"}, {"description", "Color render target index (0-7), default 0"}}},
             {"eventId",     {{"type", "integer"}, {"description", "Event ID (default: current)"}}}
         }},
         {"required", nlohmann::json::array({"x", "y"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            uint32_t x = args["x"].get<uint32_t>();
            uint32_t y = args["y"].get<uint32_t>();
            uint32_t targetIndex = args.value("targetIndex", 0u);

            std::optional<uint32_t> eventId;
            if (args.contains("eventId"))
                eventId = args["eventId"].get<uint32_t>();

            auto result = core::pickPixel(session, x, y, targetIndex, eventId);
            return to_json(result);
        }
    });

}

} // namespace renderdoc::mcp::tools
```

- [ ] **Step 2: Add declaration to tools.h**

In `src/mcp/tools/tools.h`, after line 15 (`void registerCaptureTools(...)`), add:

```cpp
void registerPixelTools(ToolRegistry& registry);
```

- [ ] **Step 3: Wire registration in mcp_server_default.cpp**

In `src/mcp/mcp_server_default.cpp`, after line 22 (`tools::registerCaptureTools(...)`), add:

```cpp
    tools::registerPixelTools(*m_registry);
```

- [ ] **Step 4: Add pixel_tools.cpp to CMakeLists.txt**

In `CMakeLists.txt`, after line 94 (`src/mcp/tools/capture_tools.cpp`), add:

```
        src/mcp/tools/pixel_tools.cpp
```

- [ ] **Step 5: Build full project**

Run: `cmake --build build --config Release 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 6: Commit**

```bash
git add src/mcp/tools/pixel_tools.cpp src/mcp/tools/tools.h src/mcp/mcp_server_default.cpp CMakeLists.txt
git commit -m "feat: add MCP tools pixel_history and pick_pixel"
```

---

### Task 7: MCP tool registration (debug_tools)

**Files:**
- Create: `src/mcp/tools/debug_tools.cpp`
- Modify: `src/mcp/tools/tools.h`
- Modify: `src/mcp/mcp_server_default.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create src/mcp/tools/debug_tools.cpp**

```cpp
#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/debug.h"

namespace renderdoc::mcp::tools {

void registerDebugTools(ToolRegistry& registry) {

    // ── debug_pixel ──────────────────────────────────────────────────────────
    registry.registerTool({
        "debug_pixel",
        "Debug the pixel/fragment shader at a specific pixel. "
        "Returns shader inputs, outputs, and optionally a full step-by-step execution trace. "
        "Variables preserve their original types (float/uint/int) and struct members.",
        {{"type", "object"},
         {"properties", {
             {"eventId",   {{"type", "integer"}, {"description", "Draw call event ID"}}},
             {"x",         {{"type", "integer"}, {"description", "Pixel X coordinate"}}},
             {"y",         {{"type", "integer"}, {"description", "Pixel Y coordinate"}}},
             {"mode",      {{"type", "string"}, {"enum", {"summary", "trace"}},
                            {"description", "summary=inputs/outputs only (default), trace=full execution"}}},
             {"primitive", {{"type", "integer"}, {"description", "Primitive ID (default: any)"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "x", "y"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            uint32_t eventId = args["eventId"].get<uint32_t>();
            uint32_t x = args["x"].get<uint32_t>();
            uint32_t y = args["y"].get<uint32_t>();
            bool fullTrace = (args.value("mode", std::string("summary")) == "trace");
            uint32_t primitive = args.value("primitive", 0xFFFFFFFFu);

            auto result = core::debugPixel(session, eventId, x, y, fullTrace, primitive);
            return to_json(result);
        }
    });

    // ── debug_vertex ─────────────────────────────────────────────────────────
    registry.registerTool({
        "debug_vertex",
        "Debug the vertex shader for a specific vertex. "
        "Returns shader inputs, outputs, and optionally a full execution trace.",
        {{"type", "object"},
         {"properties", {
             {"eventId",  {{"type", "integer"}, {"description", "Draw call event ID"}}},
             {"vertexId", {{"type", "integer"}, {"description", "Vertex index to debug"}}},
             {"mode",     {{"type", "string"}, {"enum", {"summary", "trace"}},
                           {"description", "summary (default) or trace"}}},
             {"instance", {{"type", "integer"}, {"description", "Instance index, default 0"}}},
             {"index",    {{"type", "integer"}, {"description", "Raw index buffer value for indexed draws"}}},
             {"view",     {{"type", "integer"}, {"description", "Multiview view index, default 0"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "vertexId"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            uint32_t eventId  = args["eventId"].get<uint32_t>();
            uint32_t vertexId = args["vertexId"].get<uint32_t>();
            bool fullTrace = (args.value("mode", std::string("summary")) == "trace");
            uint32_t instance = args.value("instance", 0u);
            uint32_t index    = args.value("index", 0xFFFFFFFFu);
            uint32_t view     = args.value("view", 0u);

            auto result = core::debugVertex(session, eventId, vertexId, fullTrace,
                                            instance, index, view);
            return to_json(result);
        }
    });

    // ── debug_thread ─────────────────────────────────────────────────────────
    registry.registerTool({
        "debug_thread",
        "Debug a compute shader thread at a specific workgroup and thread coordinate. "
        "Returns shader inputs, outputs, and optionally a full execution trace.",
        {{"type", "object"},
         {"properties", {
             {"eventId", {{"type", "integer"}, {"description", "Dispatch event ID"}}},
             {"groupX",  {{"type", "integer"}, {"description", "Workgroup X"}}},
             {"groupY",  {{"type", "integer"}, {"description", "Workgroup Y"}}},
             {"groupZ",  {{"type", "integer"}, {"description", "Workgroup Z"}}},
             {"threadX", {{"type", "integer"}, {"description", "Thread X within workgroup"}}},
             {"threadY", {{"type", "integer"}, {"description", "Thread Y within workgroup"}}},
             {"threadZ", {{"type", "integer"}, {"description", "Thread Z within workgroup"}}},
             {"mode",    {{"type", "string"}, {"enum", {"summary", "trace"}},
                          {"description", "summary (default) or trace"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "groupX", "groupY", "groupZ",
                                             "threadX", "threadY", "threadZ"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            uint32_t eventId = args["eventId"].get<uint32_t>();
            uint32_t gx = args["groupX"].get<uint32_t>();
            uint32_t gy = args["groupY"].get<uint32_t>();
            uint32_t gz = args["groupZ"].get<uint32_t>();
            uint32_t tx = args["threadX"].get<uint32_t>();
            uint32_t ty = args["threadY"].get<uint32_t>();
            uint32_t tz = args["threadZ"].get<uint32_t>();
            bool fullTrace = (args.value("mode", std::string("summary")) == "trace");

            auto result = core::debugThread(session, eventId, gx, gy, gz, tx, ty, tz, fullTrace);
            return to_json(result);
        }
    });

}

} // namespace renderdoc::mcp::tools
```

- [ ] **Step 2: Add declaration to tools.h**

After `registerPixelTools` declaration, add:

```cpp
void registerDebugTools(ToolRegistry& registry);
```

- [ ] **Step 3: Wire registration in mcp_server_default.cpp**

After `registerPixelTools` call, add:

```cpp
    tools::registerDebugTools(*m_registry);
```

- [ ] **Step 4: Add debug_tools.cpp to CMakeLists.txt**

After `pixel_tools.cpp` in the renderdoc-mcp-lib sources, add:

```
        src/mcp/tools/debug_tools.cpp
```

- [ ] **Step 5: Build full project**

Run: `cmake --build build --config Release 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 6: Commit**

```bash
git add src/mcp/tools/debug_tools.cpp src/mcp/tools/tools.h src/mcp/mcp_server_default.cpp CMakeLists.txt
git commit -m "feat: add MCP tools debug_pixel, debug_vertex, debug_thread"
```

---

### Task 8: MCP tool registration (texstats_tools)

**Files:**
- Create: `src/mcp/tools/texstats_tools.cpp`
- Modify: `src/mcp/tools/tools.h`
- Modify: `src/mcp/mcp_server_default.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create src/mcp/tools/texstats_tools.cpp**

```cpp
#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/texstats.h"

namespace renderdoc::mcp::tools {

void registerTexStatsTools(ToolRegistry& registry) {

    // ── get_texture_stats ────────────────────────────────────────────────────
    registry.registerTool({
        "get_texture_stats",
        "Get min/max pixel values and optionally a 256-bucket histogram for a texture. "
        "Returns typed values (float/uint/int). "
        "Useful for detecting NaN values, all-black textures, or unexpected value ranges.",
        {{"type", "object"},
         {"properties", {
             {"resourceId", {{"type", "string"},
                             {"description", "Texture resource ID (e.g. ResourceId::123)"}}},
             {"mip",        {{"type", "integer"}, {"description", "Mip level, default 0"}}},
             {"slice",      {{"type", "integer"}, {"description", "Array slice, default 0"}}},
             {"histogram",  {{"type", "boolean"}, {"description", "Include 256-bucket RGBA histogram, default false"}}},
             {"eventId",    {{"type", "integer"}, {"description", "Event ID for texture state (default: current)"}}}
         }},
         {"required", nlohmann::json::array({"resourceId"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            std::string idStr = args["resourceId"].get<std::string>();
            core::ResourceId id = parseResourceId(idStr);
            uint32_t mip   = args.value("mip", 0u);
            uint32_t slice = args.value("slice", 0u);
            bool hist      = args.value("histogram", false);

            std::optional<uint32_t> eventId;
            if (args.contains("eventId"))
                eventId = args["eventId"].get<uint32_t>();

            auto result = core::getTextureStats(session, id, mip, slice, hist, eventId);
            return to_json(result);
        }
    });

}

} // namespace renderdoc::mcp::tools
```

- [ ] **Step 2: Add declaration to tools.h**

After `registerDebugTools` declaration, add:

```cpp
void registerTexStatsTools(ToolRegistry& registry);
```

- [ ] **Step 3: Wire registration in mcp_server_default.cpp**

After `registerDebugTools` call, add:

```cpp
    tools::registerTexStatsTools(*m_registry);
```

- [ ] **Step 4: Add texstats_tools.cpp to CMakeLists.txt**

After `debug_tools.cpp` in the renderdoc-mcp-lib sources, add:

```
        src/mcp/tools/texstats_tools.cpp
```

- [ ] **Step 5: Build full project**

Run: `cmake --build build --config Release 2>&1 | tail -5`
Expected: Build succeeds with 0 errors

- [ ] **Step 6: Commit**

```bash
git add src/mcp/tools/texstats_tools.cpp src/mcp/tools/tools.h src/mcp/mcp_server_default.cpp CMakeLists.txt
git commit -m "feat: add MCP tool get_texture_stats"
```

---

### Task 9: CLI commands

**Files:**
- Modify: `src/cli/main.cpp`

- [ ] **Step 1: Add new CLI arguments to Args struct and parseArgs**

In `src/cli/main.cpp`, add new fields to the `Args` struct (line 73):

```cpp
struct Args {
    std::string capturePath;
    std::string command;
    std::vector<std::string> positional;
    std::optional<uint32_t> eventId;
    std::string filter;
    std::string typeFilter;
    std::string outputDir;
    std::string workingDir;
    std::string cmdLineArgs;
    uint32_t delayFrames = 100;
    // Phase 1 additions
    uint32_t targetIndex = 0;
    uint32_t sample = 0;
    uint32_t mipLevel = 0;
    uint32_t sliceIndex = 0;
    uint32_t instance = 0;
    uint32_t primitive = 0xFFFFFFFF;
    uint32_t index = 0xFFFFFFFF;
    uint32_t view = 0;
    bool trace = false;
    bool histogram = false;
};
```

In the `parseArgs` function's while loop (around line 139), add new option parsing before the `else` positional branch:

```cpp
        } else if (tok == "--target" && i + 1 < argc) {
            a.targetIndex = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--sample" && i + 1 < argc) {
            a.sample = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--mip" && i + 1 < argc) {
            a.mipLevel = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--slice" && i + 1 < argc) {
            a.sliceIndex = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--instance" && i + 1 < argc) {
            a.instance = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--primitive" && i + 1 < argc) {
            a.primitive = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--index" && i + 1 < argc) {
            a.index = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--view" && i + 1 < argc) {
            a.view = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--trace") {
            a.trace = true;
        } else if (tok == "--histogram") {
            a.histogram = true;
```

- [ ] **Step 2: Update printUsage with new commands**

Replace the `printUsage` function (line 86) to include Phase 1 commands:

```cpp
static void printUsage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <capture.rdc> <command> [options]\n\n"
              << "Commands:\n"
              << "  info\n"
              << "  events [--filter TEXT]\n"
              << "  draws  [--filter TEXT]\n"
              << "  pipeline [-e EID]\n"
              << "  shader STAGE [-e EID]   (STAGE: vs|hs|ds|gs|ps|cs)\n"
              << "  resources [--type TYPE]\n"
              << "  export-rt IDX -o DIR [-e EID]\n"
              << "  capture EXE [-w DIR] [-a ARGS] [-d N] [-o PATH]\n"
              << "  pixel X Y [-e EID] [--target N]\n"
              << "  pick-pixel X Y [-e EID] [--target N]\n"
              << "  debug pixel X Y -e EID [--trace] [--primitive N]\n"
              << "  debug vertex VTX -e EID [--trace] [--instance N] [--index N] [--view N]\n"
              << "  debug thread GX GY GZ TX TY TZ -e EID [--trace]\n"
              << "  tex-stats RES_ID [-e EID] [--mip N] [--slice N] [--histogram]\n";
}
```

- [ ] **Step 3: Add includes for new core modules**

At the top of `main.cpp`, after the existing includes (around line 10), add:

```cpp
#include "core/pixel.h"
#include "core/debug.h"
#include "core/texstats.h"
```

- [ ] **Step 4: Add command implementations**

After `cmdCapture` (around line 350), add:

```cpp
static void cmdPixel(Session& session, const std::vector<std::string>& positional,
                     uint32_t targetIndex, std::optional<uint32_t> eid) {
    if (positional.size() < 2) {
        std::cerr << "error: 'pixel' requires X Y coordinates\n";
        std::exit(1);
    }
    uint32_t x = static_cast<uint32_t>(std::stoul(positional[0]));
    uint32_t y = static_cast<uint32_t>(std::stoul(positional[1]));

    auto result = pixelHistory(session, x, y, targetIndex, eid);
    std::cout << "Pixel (" << x << "," << y << ") target=" << targetIndex
              << " up to event " << result.eventId << "\n";
    std::cout << result.modifications.size() << " modifications:\n\n";

    for (const auto& mod : result.modifications) {
        std::cout << "  EID " << mod.eventId
                  << "  frag=" << mod.fragmentIndex
                  << "  prim=" << mod.primitiveId
                  << "  passed=" << (mod.passed ? "yes" : "no");
        if (mod.depth.has_value())
            std::cout << "  depth=" << *mod.depth;
        if (!mod.flags.empty()) {
            std::cout << "  flags=";
            for (size_t i = 0; i < mod.flags.size(); i++) {
                if (i > 0) std::cout << ",";
                std::cout << mod.flags[i];
            }
        }
        std::cout << "\n";
        std::cout << "    post: r=" << mod.postMod.floatValue[0]
                  << " g=" << mod.postMod.floatValue[1]
                  << " b=" << mod.postMod.floatValue[2]
                  << " a=" << mod.postMod.floatValue[3] << "\n";
    }
}

static void cmdPickPixel(Session& session, const std::vector<std::string>& positional,
                         uint32_t targetIndex, std::optional<uint32_t> eid) {
    if (positional.size() < 2) {
        std::cerr << "error: 'pick-pixel' requires X Y coordinates\n";
        std::exit(1);
    }
    uint32_t x = static_cast<uint32_t>(std::stoul(positional[0]));
    uint32_t y = static_cast<uint32_t>(std::stoul(positional[1]));

    auto result = pickPixel(session, x, y, targetIndex, eid);
    std::cout << "Pixel (" << x << "," << y << ") at event " << result.eventId << ":\n"
              << "  float: " << result.color.floatValue[0] << " "
                             << result.color.floatValue[1] << " "
                             << result.color.floatValue[2] << " "
                             << result.color.floatValue[3] << "\n"
              << "  uint:  " << result.color.uintValue[0] << " "
                             << result.color.uintValue[1] << " "
                             << result.color.uintValue[2] << " "
                             << result.color.uintValue[3] << "\n";
}

static void cmdDebug(Session& session, const std::vector<std::string>& positional,
                     std::optional<uint32_t> eid, bool trace,
                     uint32_t instance, uint32_t primitive,
                     uint32_t index, uint32_t view) {
    if (positional.empty()) {
        std::cerr << "error: 'debug' requires subcommand: pixel|vertex|thread\n";
        std::exit(1);
    }
    if (!eid.has_value()) {
        std::cerr << "error: 'debug' requires -e EID\n";
        std::exit(1);
    }

    std::string sub = positional[0];
    ShaderDebugResult result;

    if (sub == "pixel") {
        if (positional.size() < 3) {
            std::cerr << "error: 'debug pixel' requires X Y\n";
            std::exit(1);
        }
        uint32_t x = static_cast<uint32_t>(std::stoul(positional[1]));
        uint32_t y = static_cast<uint32_t>(std::stoul(positional[2]));
        result = debugPixel(session, *eid, x, y, trace, primitive);
    } else if (sub == "vertex") {
        if (positional.size() < 2) {
            std::cerr << "error: 'debug vertex' requires VTX_ID\n";
            std::exit(1);
        }
        uint32_t vtx = static_cast<uint32_t>(std::stoul(positional[1]));
        result = debugVertex(session, *eid, vtx, trace, instance, index, view);
    } else if (sub == "thread") {
        if (positional.size() < 7) {
            std::cerr << "error: 'debug thread' requires GX GY GZ TX TY TZ\n";
            std::exit(1);
        }
        uint32_t gx = static_cast<uint32_t>(std::stoul(positional[1]));
        uint32_t gy = static_cast<uint32_t>(std::stoul(positional[2]));
        uint32_t gz = static_cast<uint32_t>(std::stoul(positional[3]));
        uint32_t tx = static_cast<uint32_t>(std::stoul(positional[4]));
        uint32_t ty = static_cast<uint32_t>(std::stoul(positional[5]));
        uint32_t tz = static_cast<uint32_t>(std::stoul(positional[6]));
        result = debugThread(session, *eid, gx, gy, gz, tx, ty, tz, trace);
    } else {
        std::cerr << "error: unknown debug subcommand '" << sub << "'\n";
        std::exit(1);
    }

    std::cout << "Stage: " << result.stage << "  Event: " << result.eventId
              << "  Steps: " << result.totalSteps << "\n\n";

    auto printVars = [](const std::string& label, const std::vector<DebugVariable>& vars) {
        if (vars.empty()) return;
        std::cout << label << ":\n";
        for (const auto& v : vars) {
            std::cout << "  " << v.type << " " << v.name << " = ";
            if (!v.floatValues.empty()) {
                for (size_t i = 0; i < v.floatValues.size(); i++)
                    std::cout << (i ? ", " : "") << v.floatValues[i];
            } else if (!v.intValues.empty()) {
                for (size_t i = 0; i < v.intValues.size(); i++)
                    std::cout << (i ? ", " : "") << v.intValues[i];
            } else if (!v.uintValues.empty()) {
                for (size_t i = 0; i < v.uintValues.size(); i++)
                    std::cout << (i ? ", " : "") << v.uintValues[i];
            }
            std::cout << "\n";
        }
    };

    printVars("Inputs", result.inputs);
    printVars("Outputs", result.outputs);

    if (!result.trace.empty()) {
        std::cout << "\nTrace (" << result.trace.size() << " steps):\n";
        for (const auto& step : result.trace) {
            std::cout << "  [" << step.step << "] instr=" << step.instruction;
            if (!step.file.empty())
                std::cout << " " << step.file << ":" << step.line;
            if (!step.changes.empty())
                std::cout << " (" << step.changes.size() << " changes)";
            std::cout << "\n";
        }
    }
}

static void cmdTexStats(Session& session, const std::vector<std::string>& positional,
                        std::optional<uint32_t> eid, uint32_t mip, uint32_t slice,
                        bool histogram) {
    if (positional.empty()) {
        std::cerr << "error: 'tex-stats' requires RESOURCE_ID\n";
        std::exit(1);
    }

    uint64_t resId = std::stoull(positional[0]);
    auto result = getTextureStats(session, resId, mip, slice, histogram, eid);

    std::cout << "Texture ResourceId::" << result.id << " at event " << result.eventId
              << " mip=" << result.mip << " slice=" << result.slice << "\n";
    std::cout << "Min: " << result.minVal.floatValue[0] << " "
              << result.minVal.floatValue[1] << " "
              << result.minVal.floatValue[2] << " "
              << result.minVal.floatValue[3] << "\n";
    std::cout << "Max: " << result.maxVal.floatValue[0] << " "
              << result.maxVal.floatValue[1] << " "
              << result.maxVal.floatValue[2] << " "
              << result.maxVal.floatValue[3] << "\n";

    if (!result.histogram.empty()) {
        std::cout << "\nHistogram (256 buckets):\n";
        std::cout << "bucket\tR\tG\tB\tA\n";
        for (size_t i = 0; i < result.histogram.size(); i++) {
            const auto& b = result.histogram[i];
            if (b.r || b.g || b.b || b.a) {
                std::cout << i << "\t" << b.r << "\t" << b.g
                          << "\t" << b.b << "\t" << b.a << "\n";
            }
        }
    }
}
```

- [ ] **Step 5: Add dispatch entries in main()**

In the command dispatch block (around line 381-406), add before the `else` unknown branch:

```cpp
        } else if (cmd == "pixel") {
            cmdPixel(session, args.positional, args.targetIndex, args.eventId);
        } else if (cmd == "pick-pixel") {
            cmdPickPixel(session, args.positional, args.targetIndex, args.eventId);
        } else if (cmd == "debug") {
            cmdDebug(session, args.positional, args.eventId, args.trace,
                     args.instance, args.primitive, args.index, args.view);
        } else if (cmd == "tex-stats") {
            cmdTexStats(session, args.positional, args.eventId,
                        args.mipLevel, args.sliceIndex, args.histogram);
```

- [ ] **Step 6: Build full project**

Run: `cmake --build build --config Release 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 7: Commit**

```bash
git add src/cli/main.cpp
git commit -m "feat: add CLI commands pixel, pick-pixel, debug, tex-stats"
```

---

### Task 10: Build verification and smoke test

**Files:** None (verification only)

- [ ] **Step 1: Clean rebuild**

Run:
```bash
cmake -B build -DRENDERDOC_DIR=D:/renderdoc/renderdoc -DRENDERDOC_BUILD_DIR=D:/renderdoc/renderdoc/build
cmake --build build --config Release
```
Expected: 0 errors, 0 warnings related to new code

- [ ] **Step 2: Verify MCP tool count**

Create a test script `test_tool_count.py`:
```python
import subprocess, json

proc = subprocess.Popen(
    ["build/Release/renderdoc-mcp.exe"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE
)
init = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}\n'
tools_list = '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}\n'
out, _ = proc.communicate(input=(init + tools_list).encode(), timeout=10)

for line in out.decode().strip().split('\n'):
    msg = json.loads(line)
    if msg.get("id") == 2:
        tools = msg["result"]["tools"]
        print(f"Tool count: {len(tools)}")
        for t in tools:
            print(f"  - {t['name']}")
```
Run: `python test_tool_count.py`
Expected: Tool count: 27 (21 existing + 6 new)

- [ ] **Step 3: Verify CLI help text**

Run: `build\Release\renderdoc-cli.exe`
Expected: Help text shows all new commands (pixel, pick-pixel, debug, tex-stats)

- [ ] **Step 4: Smoke test with vkcube capture (if available)**

Run:
```
build\Release\renderdoc-cli.exe tests\fixtures\vkcube.rdc pick-pixel 100 100
build\Release\renderdoc-cli.exe tests\fixtures\vkcube.rdc tex-stats 1
```
Expected: Output with pixel color values / texture min-max values (or meaningful error if resource ID invalid)

- [ ] **Step 5: Commit verification pass**

```bash
git log --oneline -8
```
Expected: See all Phase 1 commits in order

---

### Task 11: Unit tests for Phase 1 serialization

**Files:**
- Modify: `tests/unit/test_serialization.cpp`
- Modify: `tests/unit/session_stub.cpp`

- [ ] **Step 1: Add Phase 1 serialization tests to test_serialization.cpp**

Append to the end of `tests/unit/test_serialization.cpp`:

```cpp
// --- Phase 1 serialization tests ---

TEST(PixelValueSerialization, AllBranches) {
    core::PixelValue pv;
    pv.floatValue[0] = 0.5f; pv.floatValue[1] = 0.25f;
    pv.floatValue[2] = 0.125f; pv.floatValue[3] = 1.0f;
    pv.uintValue[0] = 128; pv.uintValue[1] = 64;
    pv.uintValue[2] = 32; pv.uintValue[3] = 255;
    pv.intValue[0] = -1; pv.intValue[1] = -2;
    pv.intValue[2] = -3; pv.intValue[3] = 0;

    auto j = mcp::to_json(pv);
    EXPECT_TRUE(j.contains("floatValue"));
    EXPECT_TRUE(j.contains("uintValue"));
    EXPECT_TRUE(j.contains("intValue"));
    EXPECT_EQ(j["floatValue"].size(), 4u);
    EXPECT_FLOAT_EQ(j["floatValue"][0].get<float>(), 0.5f);
    EXPECT_EQ(j["uintValue"][0], 128u);
    EXPECT_EQ(j["intValue"][0], -1);
}

TEST(PixelModificationSerialization, WithDepth) {
    core::PixelModification mod;
    mod.eventId = 42;
    mod.fragmentIndex = 0;
    mod.primitiveId = 7;
    mod.depth = 0.95f;
    mod.passed = true;
    mod.flags = {"depthTestFailed", "scissorClipped"};

    auto j = mcp::to_json(mod);
    EXPECT_EQ(j["eventId"], 42);
    EXPECT_EQ(j["primitiveId"], 7);
    EXPECT_FLOAT_EQ(j["depth"].get<float>(), 0.95f);
    EXPECT_TRUE(j["passed"].get<bool>());
    EXPECT_EQ(j["flags"].size(), 2u);
}

TEST(PixelModificationSerialization, NullDepth) {
    core::PixelModification mod;
    mod.depth = std::nullopt;

    auto j = mcp::to_json(mod);
    EXPECT_TRUE(j["depth"].is_null());
}

TEST(PixelHistoryResultSerialization, BasicFields) {
    core::PixelHistoryResult result;
    result.x = 10; result.y = 20;
    result.eventId = 50;
    result.targetIndex = 0;
    result.targetId = 123;

    auto j = mcp::to_json(result);
    EXPECT_EQ(j["x"], 10);
    EXPECT_EQ(j["y"], 20);
    EXPECT_EQ(j["eventId"], 50);
    EXPECT_EQ(j["targetId"], "ResourceId::123");
    EXPECT_TRUE(j["modifications"].is_array());
}

TEST(PickPixelResultSerialization, BasicFields) {
    core::PickPixelResult result;
    result.x = 5; result.y = 15;
    result.eventId = 30;
    result.targetIndex = 1;
    result.targetId = 456;

    auto j = mcp::to_json(result);
    EXPECT_EQ(j["x"], 5);
    EXPECT_EQ(j["targetIndex"], 1);
    EXPECT_TRUE(j.contains("color"));
    EXPECT_TRUE(j["color"].contains("floatValue"));
}

TEST(DebugVariableSerialization, FloatType) {
    core::DebugVariable var;
    var.name = "position";
    var.type = "Float";
    var.rows = 1; var.cols = 4;
    var.flags = 0;
    var.floatValues = {1.0f, 2.0f, 3.0f, 1.0f};

    auto j = mcp::to_json(var);
    EXPECT_EQ(j["name"], "position");
    EXPECT_EQ(j["type"], "Float");
    EXPECT_EQ(j["rows"], 1);
    EXPECT_EQ(j["cols"], 4);
    EXPECT_EQ(j["floatValues"].size(), 4u);
    EXPECT_TRUE(j["uintValues"].is_array());
    EXPECT_EQ(j["uintValues"].size(), 0u);
    EXPECT_TRUE(j["members"].is_array());
    EXPECT_EQ(j["members"].size(), 0u);
}

TEST(DebugVariableSerialization, WithMembers) {
    core::DebugVariable parent;
    parent.name = "cbuffer";
    parent.type = "Struct";
    parent.rows = 0; parent.cols = 0;

    core::DebugVariable child;
    child.name = "color";
    child.type = "Float";
    child.rows = 1; child.cols = 4;
    child.floatValues = {1.0f, 0.0f, 0.0f, 1.0f};
    parent.members.push_back(child);

    auto j = mcp::to_json(parent);
    EXPECT_EQ(j["members"].size(), 1u);
    EXPECT_EQ(j["members"][0]["name"], "color");
    EXPECT_EQ(j["members"][0]["floatValues"].size(), 4u);
}

TEST(DebugVariableChangeSerialization, BeforeAfter) {
    core::DebugVariableChange change;
    change.before.name = "x";
    change.before.type = "Float";
    change.before.floatValues = {0.0f};
    change.after.name = "x";
    change.after.type = "Float";
    change.after.floatValues = {1.0f};

    auto j = mcp::to_json(change);
    EXPECT_TRUE(j.contains("before"));
    EXPECT_TRUE(j.contains("after"));
    EXPECT_EQ(j["before"]["floatValues"][0], 0.0f);
    EXPECT_EQ(j["after"]["floatValues"][0], 1.0f);
}

TEST(ShaderDebugResultSerialization, SummaryMode) {
    core::ShaderDebugResult result;
    result.eventId = 100;
    result.stage = "ps";
    result.totalSteps = 42;

    core::DebugVariable input;
    input.name = "texcoord";
    input.type = "Float";
    input.rows = 1; input.cols = 2;
    input.floatValues = {0.5f, 0.5f};
    result.inputs.push_back(input);

    auto j = mcp::to_json(result);
    EXPECT_EQ(j["eventId"], 100);
    EXPECT_EQ(j["stage"], "ps");
    EXPECT_EQ(j["totalSteps"], 42);
    EXPECT_EQ(j["inputs"].size(), 1u);
    EXPECT_FALSE(j.contains("trace"));  // no trace in summary mode
}

TEST(ShaderDebugResultSerialization, WithTrace) {
    core::ShaderDebugResult result;
    result.eventId = 100;
    result.stage = "vs";
    result.totalSteps = 2;

    core::DebugStep step;
    step.step = 0;
    step.instruction = 5;
    step.file = "0";
    step.line = 10;
    result.trace.push_back(step);

    auto j = mcp::to_json(result);
    EXPECT_TRUE(j.contains("trace"));
    EXPECT_EQ(j["trace"].size(), 1u);
    EXPECT_EQ(j["trace"][0]["instruction"], 5);
    EXPECT_EQ(j["trace"][0]["line"], 10);
}

TEST(TextureStatsSerialization, WithoutHistogram) {
    core::TextureStats stats;
    stats.id = 99;
    stats.eventId = 50;
    stats.mip = 0;
    stats.slice = 0;
    stats.minVal.floatValue[0] = 0.0f;
    stats.maxVal.floatValue[0] = 1.0f;

    auto j = mcp::to_json(stats);
    EXPECT_EQ(j["id"], "ResourceId::99");
    EXPECT_EQ(j["eventId"], 50);
    EXPECT_TRUE(j.contains("min"));
    EXPECT_TRUE(j.contains("max"));
    EXPECT_FALSE(j.contains("histogram"));
}

TEST(TextureStatsSerialization, WithHistogram) {
    core::TextureStats stats;
    stats.id = 99;
    stats.eventId = 50;

    // Add 256 buckets
    stats.histogram.resize(256);
    stats.histogram[0] = {10, 20, 30, 40};
    stats.histogram[255] = {1, 2, 3, 4};

    auto j = mcp::to_json(stats);
    EXPECT_TRUE(j.contains("histogram"));
    EXPECT_EQ(j["histogram"].size(), 256u);
    EXPECT_EQ(j["histogram"][0]["r"], 10);
    EXPECT_EQ(j["histogram"][255]["a"], 4);
}
```

- [ ] **Step 2: Add Phase 1 stubs to session_stub.cpp**

Append to the end of `tests/unit/session_stub.cpp`, before the closing namespace brace, add stubs for the new core functions so the unit test target links:

```cpp
// Phase 1 stubs
PixelHistoryResult pixelHistory(const Session&, uint32_t, uint32_t, uint32_t,
                                std::optional<uint32_t>) { return {}; }
PickPixelResult pickPixel(const Session&, uint32_t, uint32_t, uint32_t,
                          std::optional<uint32_t>) { return {}; }
ShaderDebugResult debugPixel(const Session&, uint32_t, uint32_t, uint32_t,
                             bool, uint32_t) { return {}; }
ShaderDebugResult debugVertex(const Session&, uint32_t, uint32_t, bool,
                              uint32_t, uint32_t, uint32_t) { return {}; }
ShaderDebugResult debugThread(const Session&, uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t, uint32_t, uint32_t, bool) { return {}; }
TextureStats getTextureStats(const Session&, ResourceId, uint32_t, uint32_t, bool,
                             std::optional<uint32_t>) { return {}; }
```

- [ ] **Step 3: Build and run unit tests**

Run:
```
cmake --build build --config Release --target test-unit
cd build && ctest -C Release -L unit -V
```
Expected: All tests pass, including the new Phase 1 serialization tests

- [ ] **Step 4: Commit**

```bash
git add tests/unit/test_serialization.cpp tests/unit/session_stub.cpp
git commit -m "test: add unit tests for Phase 1 serialization"
```

---

### Task 12: Integration tests for Phase 1 tools

**Files:**
- Create: `tests/integration/test_tools_phase1.cpp`

- [ ] **Step 1: Create tests/integration/test_tools_phase1.cpp**

The file glob `integration/test_tools*.cpp` in tests/CMakeLists.txt auto-picks up this file.

```cpp
#include <gtest/gtest.h>
#include "mcp/tool_registry.h"
#include "mcp/tools/tools.h"
#include "core/session.h"

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;
using renderdoc::core::Session;
using renderdoc::mcp::ToolRegistry;
namespace tools = renderdoc::mcp::tools;

#ifdef _WIN32
static void openCaptureImpl(Session* s);

#pragma warning(push)
#pragma warning(disable: 4611)
static bool doOpenCaptureSEH(Session* s)
{
    __try { openCaptureImpl(s); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#pragma warning(pop)

static void openCaptureImpl(Session* s) { s->open(TEST_RDC_PATH); }
#endif

class Phase1ToolTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // Register ALL tools including Phase 1
        tools::registerSessionTools(s_registry);
        tools::registerEventTools(s_registry);
        tools::registerPipelineTools(s_registry);
        tools::registerExportTools(s_registry);
        tools::registerInfoTools(s_registry);
        tools::registerResourceTools(s_registry);
        tools::registerShaderTools(s_registry);
        tools::registerCaptureTools(s_registry);
        tools::registerPixelTools(s_registry);
        tools::registerDebugTools(s_registry);
        tools::registerTexStatsTools(s_registry);

#ifdef _WIN32
        if (!doOpenCaptureSEH(&s_session)) { s_skipAll = true; return; }
#else
        s_session.open(TEST_RDC_PATH);
#endif
        ASSERT_TRUE(s_session.isOpen());

        // Navigate to first draw
        auto draws = s_registry.callTool("list_draws", s_session, {});
        ASSERT_GT(draws["draws"].size(), 0u);
        s_firstDrawEid = draws["draws"][0]["eventId"].get<uint32_t>();
        s_registry.callTool("goto_event", s_session, {{"eventId", s_firstDrawEid}});

        // Find a texture resource for tex-stats tests
        auto resources = s_registry.callTool("list_resources", s_session,
            {{"type", "Texture"}});
        if (resources.is_array() && resources.size() > 0) {
            s_textureResId = resources[0]["resourceId"].get<std::string>();
        }
    }

    static void TearDownTestSuite() { s_session.close(); }

    void SetUp() override {
        if (s_skipAll)
            GTEST_SKIP() << "RenderDoc replay not available";
    }

    static Session s_session;
    static ToolRegistry s_registry;
    static uint32_t s_firstDrawEid;
    static std::string s_textureResId;
    static bool s_skipAll;
};

Session Phase1ToolTest::s_session;
ToolRegistry Phase1ToolTest::s_registry;
uint32_t Phase1ToolTest::s_firstDrawEid = 0;
std::string Phase1ToolTest::s_textureResId;
bool Phase1ToolTest::s_skipAll = false;

// -- pick_pixel ---------------------------------------------------------------

TEST_F(Phase1ToolTest, PickPixel_ReturnsColor) {
    auto result = s_registry.callTool("pick_pixel", s_session,
        {{"x", 0}, {"y", 0}});
    EXPECT_TRUE(result.contains("color"));
    EXPECT_TRUE(result["color"].contains("floatValue"));
    EXPECT_TRUE(result["color"].contains("uintValue"));
    EXPECT_TRUE(result["color"].contains("intValue"));
    EXPECT_EQ(result["color"]["floatValue"].size(), 4u);
    EXPECT_EQ(result["x"], 0);
    EXPECT_EQ(result["y"], 0);
}

TEST_F(Phase1ToolTest, PickPixel_WithEventId) {
    auto result = s_registry.callTool("pick_pixel", s_session,
        {{"x", 0}, {"y", 0}, {"eventId", s_firstDrawEid}});
    EXPECT_EQ(result["eventId"], s_firstDrawEid);
}

TEST_F(Phase1ToolTest, PickPixel_OutOfBounds_Throws) {
    EXPECT_THROW(
        s_registry.callTool("pick_pixel", s_session,
            {{"x", 99999}, {"y", 99999}}),
        std::exception);
}

TEST_F(Phase1ToolTest, PickPixel_InvalidTarget_Throws) {
    EXPECT_THROW(
        s_registry.callTool("pick_pixel", s_session,
            {{"x", 0}, {"y", 0}, {"targetIndex", 99}}),
        std::exception);
}

// -- pixel_history ------------------------------------------------------------

TEST_F(Phase1ToolTest, PixelHistory_ReturnsModifications) {
    auto result = s_registry.callTool("pixel_history", s_session,
        {{"x", 0}, {"y", 0}, {"eventId", s_firstDrawEid}});
    EXPECT_TRUE(result.contains("modifications"));
    EXPECT_TRUE(result["modifications"].is_array());
    EXPECT_TRUE(result.contains("targetId"));
    EXPECT_EQ(result["x"], 0);
}

TEST_F(Phase1ToolTest, PixelHistory_ModificationFields) {
    auto result = s_registry.callTool("pixel_history", s_session,
        {{"x", 0}, {"y", 0}, {"eventId", s_firstDrawEid}});
    if (result["modifications"].size() > 0) {
        auto& mod = result["modifications"][0];
        EXPECT_TRUE(mod.contains("eventId"));
        EXPECT_TRUE(mod.contains("shaderOut"));
        EXPECT_TRUE(mod.contains("postMod"));
        EXPECT_TRUE(mod.contains("passed"));
        EXPECT_TRUE(mod.contains("flags"));
    }
}

// -- debug_pixel --------------------------------------------------------------

TEST_F(Phase1ToolTest, DebugPixel_SummaryMode) {
    // Debug at center of render target to maximize chance of hitting a fragment
    auto result = s_registry.callTool("debug_pixel", s_session,
        {{"eventId", s_firstDrawEid}, {"x", 50}, {"y", 50}});
    EXPECT_EQ(result["eventId"], s_firstDrawEid);
    EXPECT_TRUE(result.contains("stage"));
    EXPECT_TRUE(result.contains("totalSteps"));
    EXPECT_TRUE(result.contains("inputs"));
    EXPECT_TRUE(result.contains("outputs"));
    EXPECT_FALSE(result.contains("trace"));  // summary mode
}

TEST_F(Phase1ToolTest, DebugPixel_TraceMode) {
    auto result = s_registry.callTool("debug_pixel", s_session,
        {{"eventId", s_firstDrawEid}, {"x", 50}, {"y", 50}, {"mode", "trace"}});
    EXPECT_TRUE(result.contains("trace"));
    if (result["trace"].size() > 0) {
        auto& step = result["trace"][0];
        EXPECT_TRUE(step.contains("step"));
        EXPECT_TRUE(step.contains("instruction"));
        EXPECT_TRUE(step.contains("changes"));
    }
}

TEST_F(Phase1ToolTest, DebugPixel_InputsHaveTypeInfo) {
    auto result = s_registry.callTool("debug_pixel", s_session,
        {{"eventId", s_firstDrawEid}, {"x", 50}, {"y", 50}});
    if (result["inputs"].size() > 0) {
        auto& input = result["inputs"][0];
        EXPECT_TRUE(input.contains("name"));
        EXPECT_TRUE(input.contains("type"));
        EXPECT_TRUE(input.contains("rows"));
        EXPECT_TRUE(input.contains("cols"));
    }
}

// -- debug_vertex -------------------------------------------------------------

TEST_F(Phase1ToolTest, DebugVertex_SummaryMode) {
    auto result = s_registry.callTool("debug_vertex", s_session,
        {{"eventId", s_firstDrawEid}, {"vertexId", 0}});
    EXPECT_EQ(result["eventId"], s_firstDrawEid);
    EXPECT_EQ(result["stage"], "vs");
    EXPECT_TRUE(result.contains("inputs"));
    EXPECT_TRUE(result.contains("outputs"));
}

// -- get_texture_stats --------------------------------------------------------

TEST_F(Phase1ToolTest, GetTextureStats_MinMax) {
    if (s_textureResId.empty()) GTEST_SKIP() << "No texture resource found";

    auto result = s_registry.callTool("get_texture_stats", s_session,
        {{"resourceId", s_textureResId}});
    EXPECT_TRUE(result.contains("min"));
    EXPECT_TRUE(result.contains("max"));
    EXPECT_TRUE(result["min"].contains("floatValue"));
    EXPECT_TRUE(result["max"].contains("floatValue"));
    EXPECT_FALSE(result.contains("histogram"));
}

TEST_F(Phase1ToolTest, GetTextureStats_WithHistogram) {
    if (s_textureResId.empty()) GTEST_SKIP() << "No texture resource found";

    auto result = s_registry.callTool("get_texture_stats", s_session,
        {{"resourceId", s_textureResId}, {"histogram", true}});
    EXPECT_TRUE(result.contains("histogram"));
    EXPECT_EQ(result["histogram"].size(), 256u);
    auto& bucket = result["histogram"][0];
    EXPECT_TRUE(bucket.contains("r"));
    EXPECT_TRUE(bucket.contains("g"));
    EXPECT_TRUE(bucket.contains("b"));
    EXPECT_TRUE(bucket.contains("a"));
}

TEST_F(Phase1ToolTest, GetTextureStats_InvalidResource_Throws) {
    EXPECT_THROW(
        s_registry.callTool("get_texture_stats", s_session,
            {{"resourceId", "ResourceId::999999999"}}),
        std::exception);
}

TEST_F(Phase1ToolTest, GetTextureStats_InvalidMip_Throws) {
    if (s_textureResId.empty()) GTEST_SKIP() << "No texture resource found";

    EXPECT_THROW(
        s_registry.callTool("get_texture_stats", s_session,
            {{"resourceId", s_textureResId}, {"mip", 99}}),
        std::exception);
}
```

- [ ] **Step 2: Build and run integration tests**

Run:
```
cmake --build build --config Release --target test-tools
cd build && ctest -C Release -L integration -R Phase1 -V
```
Expected: All Phase 1 integration tests pass (may SKIP on headless machines)

- [ ] **Step 3: Commit**

```bash
git add tests/integration/test_tools_phase1.cpp
git commit -m "test: add integration tests for Phase 1 tools"
```

---

### Task 13: Update SKILL.md with new tools (was Task 11)

**Files:**
- Modify: `skills/renderdoc-mcp/SKILL.md`

- [ ] **Step 1: Add new tools to the tool reference section in SKILL.md**

Find the tool reference section and add entries for all 6 new tools with descriptions and parameter summaries. Add a new diagnostic workflow section:

```markdown
### Pixel-Level Diagnosis

When investigating why a pixel has the wrong color or is missing:

1. **pick_pixel** — Read the current pixel color to confirm the issue
2. **pixel_history** — Find which draws modified this pixel, check if any were culled/discarded
3. **debug_pixel** — Trace the fragment shader execution to find where the wrong value comes from
4. **get_texture_stats** — Check if input textures have unexpected ranges (NaN, all-zero, etc.)

### Shader Debugging

When a draw produces wrong output:

1. **debug_vertex** / **debug_pixel** — Trace shader execution with mode="summary" first
2. If inputs look wrong, check bindings with **get_bindings**
3. If logic seems wrong, re-run with mode="trace" for step-by-step execution
```

- [ ] **Step 2: Commit**

```bash
git add skills/renderdoc-mcp/SKILL.md
git commit -m "docs: update SKILL.md with Phase 1 tools and workflows"
```

---

### Task 14: Update README with new tool count and descriptions

**Files:**
- Modify: `README.md`
- Modify: `README-CN.md`

- [ ] **Step 1: Update tool count and add Phase 1 tool descriptions to README.md**

Update the total tool count from 21 to 27 and add descriptions of the 6 new tools in the appropriate sections.

- [ ] **Step 2: Update README-CN.md similarly**

Mirror the English changes in the Chinese README.

- [ ] **Step 3: Commit**

```bash
git add README.md README-CN.md
git commit -m "docs: update READMEs for Phase 1 (27 tools)"
```
