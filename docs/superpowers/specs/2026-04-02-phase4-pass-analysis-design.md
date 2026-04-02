# Phase 4: Pass-Level Analysis — Design Spec

**Date**: 2026-04-02
**Status**: Approved (revised)
**Scope**: 4 new MCP tools, 3 CLI sub-commands, core library module
**Tool count**: 48 → 52

## Overview

Add pass-level analysis capabilities to renderdoc-mcp: attachment inspection, per-pass statistics, inter-pass dependency DAG, and unused render target detection. These features fill the gap between draw-level inspection (Phase 0-1) and frame-level diff (Phase 3), giving AI agents a mid-level view of frame structure for optimization and debugging.

Reference implementation: rdc-cli `query_service.py` (passes, deps, unused targets).

## Pass Enumeration Strategy

The existing `listPasses()` only recognizes **marker-delimited** passes (top-level actions whose children contain draw/dispatch calls). Many captures — including the `vkcube.rdc` test fixture — lack user markers entirely, so `listPasses()` returns an empty vector for them.

Phase 4 introduces a **two-tier pass resolution** with automatic fallback:

1. **Explicit passes**: Use `listPasses()` as-is (marker groups with draws).
2. **Synthetic passes**: When explicit passes are empty, infer passes from **output-target changes**. Walk root actions sequentially; consecutive draw/dispatch/clear actions sharing the same set of bound color + depth targets form one synthetic pass. Name each synthetic pass by its target set (e.g. `"RT0+Depth"`, `"Swapchain"`).

The core function `enumeratePassRanges()` returns a unified `vector<PassRange>` regardless of source:

```cpp
struct PassRange {
    std::string name;
    uint32_t beginEventId = 0;  // first event in pass
    uint32_t endEventId = 0;    // last event in pass (inclusive)
    bool synthetic = false;     // true if inferred, false if marker-based
};

std::vector<PassRange> enumeratePassRanges(const Session& session);
```

All four Phase 4 tools build on `enumeratePassRanges()`, never on `listPasses()` directly. This guarantees non-empty results on any capture that contains at least one draw call.

## New MCP Tools

### 1. `get_pass_attachments`

**Purpose**: Query color and depth attachments for a specific render pass.

**Parameters**:
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `eventId` | integer | yes | Event ID of the pass marker |

**Returns**:
```json
{
  "passName": "Shadow Map",
  "eventId": 42,
  "colorTargets": [
    {
      "resourceId": "ResourceId::15",
      "name": "ShadowRT",
      "format": "R32_FLOAT",
      "width": 2048,
      "height": 2048
    }
  ],
  "depthTarget": {
    "resourceId": "ResourceId::16",
    "name": "ShadowDepth",
    "format": "D32_FLOAT",
    "width": 2048,
    "height": 2048
  },
  "hasDepth": true,
  "synthetic": false
}
```

**Implementation**: Navigate to pass's first draw call via `ctrl->SetFrameEvent()`, read pipeline state's output merger / framebuffer attachments. Extract resource ID, name, format, and dimensions from the bound output targets.

**Note on load/store ops**: The original design included `loadOp`/`storeOp` fields, but pipeline state only exposes attachment ID, format, and dimensions — not load/store semantics. Extracting load/store ops would require Vulkan-specific structured data parsing (action chunk metadata), which is not portable across D3D11/D3D12/OpenGL. These fields are **removed** from the initial implementation. They may be added later as an optional Vulkan-only extension if needed.

### 2. `get_pass_statistics`

**Purpose**: Return per-pass aggregated statistics for the entire frame.

**Parameters**: None.

**Returns**:
```json
{
  "passes": [
    {
      "name": "Shadow Map",
      "eventId": 42,
      "drawCount": 120,
      "dispatchCount": 0,
      "totalTriangles": 450000,
      "rtWidth": 2048,
      "rtHeight": 2048,
      "attachmentCount": 2
    },
    {
      "name": "GBuffer",
      "eventId": 200,
      "drawCount": 85,
      "dispatchCount": 0,
      "totalTriangles": 320000,
      "rtWidth": 1920,
      "rtHeight": 1080,
      "attachmentCount": 4
    }
  ],
  "count": 2
}
```

**Implementation**: Call `enumeratePassRanges()` to get unified pass list (marker or synthetic). For each pass range, count draws/dispatches/triangles by walking actions within `[beginEventId, endEventId]`. For RT dimensions and attachment count, navigate to the first draw in the range and read pipeline state output targets.

### 3. `get_pass_deps`

**Purpose**: Build inter-pass resource dependency DAG.

**Parameters**: None.

**Returns**:
```json
{
  "edges": [
    {
      "srcPass": "Shadow Map",
      "dstPass": "Lighting",
      "resources": ["ResourceId::15"]
    },
    {
      "srcPass": "GBuffer",
      "dstPass": "Lighting",
      "resources": ["ResourceId::20", "ResourceId::21", "ResourceId::22"]
    }
  ],
  "passCount": 3,
  "edgeCount": 2
}
```

**Implementation**:
1. Enumerate all passes and their event ID ranges.
2. For each resource in the capture, call `getResourceUsage()` to get per-event usage records.
3. Classify each usage as **write** or **read**:
   - Write: `ColorTarget`, `DepthStencilTarget`, `CopyDst`, `Clear`, `GenMips`
   - Read: `VS_Resource` through `CS_Resource`, `VertexBuffer`, `IndexBuffer`, `CopySrc`, `Indirect`, `Constants`
4. Bucket each usage event into its containing pass by event ID range.
5. For each resource R: if pass A writes R and pass B reads R (and A precedes B), emit edge A → B with resource R.
6. Deduplicate edges, merge resource lists.

### 4. `find_unused_targets`

**Purpose**: Detect render targets that are written but never consumed by visible output.

**Parameters**: None.

**Returns**:
```json
{
  "unused": [
    {
      "resourceId": "ResourceId::30",
      "name": "DebugOverlay_RT",
      "writtenBy": ["Debug Pass"],
      "wave": 1
    }
  ],
  "unusedCount": 1,
  "totalTargets": 8
}
```

**Implementation** (reverse reachability, reference: rdc-cli `find_unused_targets`):
1. Collect all resources that appear as write targets (ColorTarget, DepthStencilTarget) in any pass.
2. Mark **always-live** resources: swapchain images (presentable textures), depth targets used by subsequent passes.
3. **Reverse walk**: For each pass (in reverse order), if the pass writes any live resource, mark all its read inputs as live.
4. **Iterate** until the live set converges (no new resources marked).
5. Resources written but not marked live are **unused**.
6. **Wave assignment**: assign wave numbers by iterative leaf pruning — wave 1 resources have no consumers at all, wave 2 resources only feed wave 1 resources, etc. Higher wave = more obviously unused.

Swapchain detection: use `ctrl->GetTextures()` and check for presentable/swapchain usage flags.

## New Types (`src/core/types.h`)

```cpp
// --- Phase 4: Pass Analysis ---

// Unified pass range from enumeratePassRanges() — works for both
// marker-based and synthetic (output-target-change) passes.
struct PassRange {
    std::string name;
    uint32_t beginEventId = 0;
    uint32_t endEventId = 0;
    bool synthetic = false;
};

struct AttachmentInfo {
    ResourceId resourceId = 0;  // serialized as "ResourceId::N" on the wire
    std::string name;
    std::string format;
    uint32_t width = 0;
    uint32_t height = 0;
    // Note: loadOp/storeOp intentionally omitted — not portable across APIs.
    // May be added later as Vulkan-only optional fields.
};

struct PassAttachments {
    std::string passName;
    uint32_t eventId = 0;
    std::vector<AttachmentInfo> colorTargets;
    AttachmentInfo depthTarget;
    bool hasDepth = false;
    bool synthetic = false;
};

struct PassStatistics {
    std::string name;
    uint32_t eventId = 0;
    uint32_t drawCount = 0;
    uint32_t dispatchCount = 0;
    uint64_t totalTriangles = 0;
    uint32_t rtWidth = 0;
    uint32_t rtHeight = 0;
    uint32_t attachmentCount = 0;
    bool synthetic = false;
};

struct PassEdge {
    std::string srcPass;
    std::string dstPass;
    std::vector<ResourceId> sharedResources;  // serialized as "ResourceId::N"
};

struct PassDependencyGraph {
    std::vector<PassEdge> edges;
    uint32_t passCount = 0;
    uint32_t edgeCount = 0;
};

struct UnusedTarget {
    ResourceId resourceId = 0;  // serialized as "ResourceId::N"
    std::string name;
    std::vector<std::string> writtenBy;
    uint32_t wave = 0;
};

struct UnusedTargetResult {
    std::vector<UnusedTarget> unused;
    uint32_t unusedCount = 0;
    uint32_t totalTargets = 0;
};
```

All `ResourceId` fields use the existing `resourceIdToString()` / `parseResourceId()` functions from `serialization.h` to maintain the canonical `"ResourceId::N"` wire format.

## Core Layer (`src/core/pass_analysis.h/.cpp`)

New module with 5 public functions:

```cpp
namespace core {
    // Pass enumeration with automatic fallback (marker → synthetic)
    std::vector<PassRange> enumeratePassRanges(const Session& session);

    // Phase 4 tools — all build on enumeratePassRanges(), not listPasses()
    PassAttachments   getPassAttachments(const Session& session, uint32_t eventId);
    std::vector<PassStatistics> getPassStatistics(const Session& session);
    PassDependencyGraph getPassDependencies(const Session& session);
    UnusedTargetResult  findUnusedTargets(const Session& session);
}
```

Dependencies:
- `session.h` — replay controller access
- `resources.h` — `listPasses()` (used only as first tier in `enumeratePassRanges`), `listResources()`
- `usage.h` — `getResourceUsage()`
- `pipeline.h` — `getPipelineState()` for attachment info
- `events.h` — action tree traversal for synthetic pass inference

## MCP Tool Layer (`src/mcp/tools/pass_tools.cpp`)

New file with 4 tool registrations following existing patterns in `resource_tools.cpp`. Each tool:
1. Extracts parameters from JSON args
2. Calls core function
3. Serializes result via `to_json()` helpers

## Serialization (`src/mcp/serialization.cpp`)

Add `to_json()` overloads for all new types:
- `to_json(PassRange)`
- `to_json(AttachmentInfo)` — uses `resourceIdToString()` for ID field
- `to_json(PassAttachments)`
- `to_json(PassStatistics)`
- `to_json(PassEdge)` — uses `resourceIdToString()` for resource list
- `to_json(PassDependencyGraph)`
- `to_json(UnusedTarget)` — uses `resourceIdToString()` for ID field
- `to_json(UnusedTargetResult)`

## CLI Layer (`src/cli/main.cpp`)

3 new commands following the existing `<capture.rdc> <command> [options]` convention:

| Command | Description |
|---------|-------------|
| `renderdoc-cli <capture.rdc> pass-stats` | Per-pass statistics (JSON) |
| `renderdoc-cli <capture.rdc> pass-deps` | Dependency DAG (JSON) |
| `renderdoc-cli <capture.rdc> unused-targets` | Unused RT report (JSON) |

These follow the same parsing path as existing commands (`info`, `events`, `draws`, etc.) — the capture path is `argv[1]`, command is `argv[2]`. No parser restructuring required. The `diff` special case (`diff FILE_A FILE_B`) remains untouched.

Output format: JSON (consistent with existing CLI commands).

## Testing

### Unit Tests (`tests/unit/test_pass_analysis.cpp`)

- **Dependency DAG algorithm**: Given mock usage data, verify correct edges.
- **Wave assignment**: Given known unused targets, verify wave numbers.
- **Edge deduplication**: Multiple shared resources between same passes merge correctly.

### Integration Tests (`tests/integration/test_tools_phase4.cpp`)

Using `vkcube.rdc` fixture. **Important**: vkcube is a minimal Vulkan sample that may lack marker-delimited passes, so tests must account for synthetic pass fallback.

- `get_pass_attachments`: Call with eventId of any draw; verify response contains at least one color target with non-zero width/height and a valid `"ResourceId::N"` string. Do **not** assert specific pass names.
- `get_pass_statistics`: Verify `count >= 1` (synthetic passes guaranteed if draws exist). Verify each entry has `drawCount > 0` or `dispatchCount > 0`. Cross-check total draw count against `list_draws` tool.
- `get_pass_deps`: Verify response structure (`edges` array, `passCount`, `edgeCount`). Accept `edgeCount == 0` as valid — vkcube may have a single pass with no inter-pass dependencies. Assert `passCount >= 1`.
- `find_unused_targets`: Verify response structure. Accept `unusedCount == 0` as valid (well-optimized capture).

### CLI Tests

- `passes --stats`: Verify JSON output parses and contains expected fields.
- `unused-targets`: Verify JSON output structure.

## File Changes Summary

| File | Action | Estimated Lines |
|------|--------|-----------------|
| `src/core/types.h` | Edit — add new types | +60 |
| `src/core/pass_analysis.h` | **New** — function declarations | ~30 |
| `src/core/pass_analysis.cpp` | **New** — core implementations | ~400 |
| `src/mcp/tools/pass_tools.cpp` | **New** — 4 tool registrations | ~150 |
| `src/mcp/serialization.h` | Edit — declare new to_json overloads | +15 |
| `src/mcp/serialization.cpp` | Edit — implement new to_json overloads | +80 |
| `src/mcp/tool_registry.cpp` | Edit — register pass tools | +5 |
| `src/cli/main.cpp` | Edit — add CLI sub-commands | +80 |
| `tests/unit/test_pass_analysis.cpp` | **New** — unit tests | ~150 |
| `tests/integration/test_tools_phase4.cpp` | **New** — integration tests | ~200 |
| `CMakeLists.txt` | Edit — add new source files | +10 |
| `README.md` | Edit — document new tools | +40 |
| **Total** | | **~1,220 lines** |

## Success Criteria

1. All 4 tools return valid JSON responses for `vkcube.rdc`.
2. `get_pass_deps` correctly identifies resource flow between passes.
3. `find_unused_targets` returns empty list for well-optimized captures, non-empty for captures with dead passes.
4. All unit and integration tests pass.
5. CLI sub-commands produce parseable JSON output.
6. Tool count reaches 52.

## Non-Goals

- VFS-style path navigation (rdc-cli feature, not suited for MCP tool model)
- Remote replay support (separate Phase)
- Load/store op extraction — requires Vulkan-specific structured data parsing, not portable across D3D11/D3D12/OpenGL. May be added later as optional Vulkan-only extension.
- Graphviz/ASCII graph rendering (JSON DAG is sufficient for AI consumption)

## Review Revision Log

**2026-04-02 rev1** — Addressed 5 review findings:
1. **(P1) Markerless capture support**: Added two-tier pass enumeration (`enumeratePassRanges`) with synthetic pass fallback based on output-target changes. All tools build on this, not `listPasses()` directly.
2. **(P1) loadOp/storeOp removed**: Not portable across graphics APIs. Pipeline state only exposes attachment ID/format/dimensions. Fields removed from `AttachmentInfo`.
3. **(P1) CLI syntax fixed**: Changed from `passes --stats <capture>` to `<capture.rdc> pass-stats`, matching existing `<capture.rdc> <command>` convention.
4. **(P2) ResourceId wire format**: All `resourceId` fields now use `"ResourceId::N"` canonical string format via existing `resourceIdToString()`.
5. **(P2) Integration test expectations relaxed**: Removed assumption of marker-based passes or multi-pass dependencies in vkcube.rdc. Tests verify structure and invariants, not specific pass topology.
