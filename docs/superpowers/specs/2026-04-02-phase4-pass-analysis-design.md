# Phase 4: Pass-Level Analysis — Design Spec

**Date**: 2026-04-02
**Status**: Approved
**Scope**: 4 new MCP tools, 3 CLI sub-commands, core library module
**Tool count**: 48 → 52

## Overview

Add pass-level analysis capabilities to renderdoc-mcp: attachment inspection, per-pass statistics, inter-pass dependency DAG, and unused render target detection. These features fill the gap between draw-level inspection (Phase 0-1) and frame-level diff (Phase 3), giving AI agents a mid-level view of frame structure for optimization and debugging.

Reference implementation: rdc-cli `query_service.py` (passes, deps, unused targets).

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
      "resourceId": 15,
      "name": "ShadowRT",
      "format": "R32_FLOAT",
      "width": 2048,
      "height": 2048,
      "loadOp": "Clear",
      "storeOp": "Store"
    }
  ],
  "depthTarget": {
    "resourceId": 16,
    "name": "ShadowDepth",
    "format": "D32_FLOAT",
    "width": 2048,
    "height": 2048,
    "loadOp": "Clear",
    "storeOp": "DontCare"
  },
  "hasDepth": true
}
```

**Implementation**: Navigate to pass's first draw call via `ctrl->SetFrameEvent()`, read pipeline state's output merger / framebuffer attachments. Extract format, dimensions, and load/store ops from the action's begin-pass metadata.

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

**Implementation**: Iterate all passes from `listPasses()`. For each pass, count draws/dispatches/triangles recursively. For RT dimensions and attachment count, read the first draw's pipeline state output targets.

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
      "resources": [15]
    },
    {
      "srcPass": "GBuffer",
      "dstPass": "Lighting",
      "resources": [20, 21, 22]
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
      "resourceId": 30,
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

struct AttachmentInfo {
    uint64_t resourceId = 0;
    std::string name;
    std::string format;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string loadOp;   // "Load", "Clear", "DontCare"
    std::string storeOp;  // "Store", "DontCare"
};

struct PassAttachments {
    std::string passName;
    uint32_t eventId = 0;
    std::vector<AttachmentInfo> colorTargets;
    AttachmentInfo depthTarget;
    bool hasDepth = false;
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
};

struct PassEdge {
    std::string srcPass;
    std::string dstPass;
    std::vector<uint64_t> sharedResources;
};

struct PassDependencyGraph {
    std::vector<PassEdge> edges;
    uint32_t passCount = 0;
    uint32_t edgeCount = 0;
};

struct UnusedTarget {
    uint64_t resourceId = 0;
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

## Core Layer (`src/core/pass_analysis.h/.cpp`)

New module with 4 public functions:

```cpp
namespace core {
    PassAttachments   getPassAttachments(const Session& session, uint32_t eventId);
    std::vector<PassStatistics> getPassStatistics(const Session& session);
    PassDependencyGraph getPassDependencies(const Session& session);
    UnusedTargetResult  findUnusedTargets(const Session& session);
}
```

Dependencies:
- `session.h` — replay controller access
- `resources.h` — `listPasses()`, `getPassInfo()`, `listResources()`
- `usage.h` — `getResourceUsage()`
- `pipeline.h` — `getPipelineState()` for attachment info

## MCP Tool Layer (`src/mcp/tools/pass_tools.cpp`)

New file with 4 tool registrations following existing patterns in `resource_tools.cpp`. Each tool:
1. Extracts parameters from JSON args
2. Calls core function
3. Serializes result via `to_json()` helpers

## Serialization (`src/mcp/serialization.cpp`)

Add `to_json()` overloads for all new types:
- `to_json(AttachmentInfo)`
- `to_json(PassAttachments)`
- `to_json(PassStatistics)`
- `to_json(PassEdge)`
- `to_json(PassDependencyGraph)`
- `to_json(UnusedTarget)`
- `to_json(UnusedTargetResult)`

## CLI Layer (`src/cli/main.cpp`)

3 new sub-commands under the `passes` group:

| Command | Description |
|---------|-------------|
| `renderdoc-cli passes --stats <capture>` | Per-pass statistics table |
| `renderdoc-cli passes --deps <capture>` | Dependency DAG (JSON) |
| `renderdoc-cli unused-targets <capture>` | Unused RT report |

Output format: JSON (consistent with existing CLI commands).

## Testing

### Unit Tests (`tests/unit/test_pass_analysis.cpp`)

- **Dependency DAG algorithm**: Given mock usage data, verify correct edges.
- **Wave assignment**: Given known unused targets, verify wave numbers.
- **Edge deduplication**: Multiple shared resources between same passes merge correctly.

### Integration Tests (`tests/integration/test_tools_phase4.cpp`)

Using `vkcube.rdc` fixture:
- `get_pass_attachments`: Verify attachment format, dimensions for known pass.
- `get_pass_statistics`: Verify draw/dispatch counts match `list_passes` data.
- `get_pass_deps`: Verify at least one edge exists (VkCube has shadow → main pass dependency).
- `find_unused_targets`: Verify result structure; VkCube may have 0 unused targets (valid).

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
- Load/store op extraction from Vulkan-specific API calls (attachment info uses pipeline state, not API-call parsing)
- Graphviz/ASCII graph rendering (JSON DAG is sufficient for AI consumption)
