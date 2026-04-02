# Diff Engine Design — renderdoc-mcp Phase 3

**Date:** 2026-04-02
**Status:** Draft
**Scope:** Dual-session capture comparison with 6 diff modes, 8 MCP tools, 6 CLI commands

---

## 1. Overview

Add a diff engine to renderdoc-mcp that compares two RenderDoc capture files (.rdc) side by side. This enables AI-assisted frame regression diagnosis: load two captures (before/after a change), identify what differs at the draw, resource, pipeline, and pixel level.

**Reference:** Adapted from rdc-cli's Python diff engine (~2,200 LOC), translated to C++17 within renderdoc-mcp's layered architecture (core → MCP → CLI → Skill).

### Goals

- Compare two captures across 6 dimensions: summary, draws, resources, passes/stats, pipeline state, framebuffer pixels
- Maintain architectural consistency with existing Phase 0–2 modules
- Support both MCP tool and CLI command interfaces
- Enable CI-friendly exit codes (0 = identical, 1 = differences, 2 = error)

### Non-Goals

- 3-way merge or diff
- Remote replay diff (future phase)
- Real-time diff during capture (snapshot-only)

---

## 2. Architecture

### 2.1 Approach: Core Diff Module + Independent DiffSession

**Decision:** New `DiffSession` class, fully independent from existing `Session`. Both can coexist in the same process. No modifications to existing Session code.

**Rationale:** RenderDoc supports multiple `IReplayController` instances in a single process. A separate class avoids Session responsibility bloat and keeps diff lifecycle management self-contained. This is simpler than rdc-cli's dual-daemon approach (which uses separate OS processes) because C++ can hold both controllers in-process.

### 2.2 New Files

```
src/core/
  diff_session.h       — DiffSession class declaration
  diff_session.cpp     — Dual capture open/close lifecycle
  diff.h               — Diff algorithm declarations + result types
  diff.cpp             — All 6 diff algorithm implementations

src/mcp/tools/
  diff_tools.cpp       — 8 MCP tool registrations

src/mcp/
  serialization.h/cpp  — to_json for all new diff types (extend existing)

src/cli/
  main.cpp             — 6 new CLI diff subcommands (extend existing)

tests/integration/
  test_tools_diff.cpp  — Integration tests for diff tools
tests/unit/
  test_diff_algo.cpp   — Unit tests for LCS alignment algorithm
```

### 2.3 Build Integration

Add to CMakeLists.txt:
- `src/core/diff_session.cpp` and `src/core/diff.cpp` to `renderdoc-core` static library
- `src/mcp/tools/diff_tools.cpp` to `renderdoc-mcp-lib`
- New test executables: `test-diff-algo` (unit), `test-diff-tools` (integration)

### 2.4 Coexistence with Session

The MCP server holds both:

```cpp
renderdoc::core::Session session;           // existing: single-capture operations
renderdoc::core::DiffSession diffSession;   // new: dual-capture diff operations
```

Both are independently managed. Opening a diff session does NOT close the regular session, and vice versa.

---

## 3. DiffSession Class

```cpp
namespace renderdoc::core {

class DiffSession {
public:
    DiffSession();
    ~DiffSession();  // calls close()

    DiffSession(const DiffSession&) = delete;
    DiffSession& operator=(const DiffSession&) = delete;

    struct OpenResult {
        CaptureInfo infoA;
        CaptureInfo infoB;
    };

    // Opens both captures. Throws CoreError on failure.
    // If already open, throws DiffAlreadyOpen.
    OpenResult open(const std::string& pathA, const std::string& pathB);

    // Closes both sessions. Safe to call multiple times.
    void close();

    bool isOpen() const;

    // Internal accessors for diff algorithms
    IReplayController* controllerA() const;
    IReplayController* controllerB() const;
    ICaptureFile* captureFileA() const;
    ICaptureFile* captureFileB() const;
    const std::string& pathA() const;
    const std::string& pathB() const;

private:
    ICaptureFile* m_capA = nullptr;
    ICaptureFile* m_capB = nullptr;
    IReplayController* m_ctrlA = nullptr;
    IReplayController* m_ctrlB = nullptr;
    std::string m_pathA, m_pathB;
};

} // namespace renderdoc::core
```

### Lifecycle

1. **open(pathA, pathB):**
   - Create two `ICaptureFile` instances via `RENDERDOC_OpenCaptureFile`
   - Open replay on each: `cap->OpenCapture(ReplayOptions(), &controller)`
   - If A succeeds but B fails: close A, then throw
   - Store both controllers and paths
   - Return `OpenResult` with metadata from both captures

2. **close():**
   - Shutdown controllers (if non-null): `controller->Shutdown()`
   - Shutdown capture files: `cap->Shutdown()`
   - Null all pointers

3. **Destructor:** Calls `close()`.

---

## 4. Data Types

All new types in `src/core/diff.h` within `namespace renderdoc::core`:

### 4.1 Common

```cpp
enum class DiffStatus { Equal, Modified, Added, Deleted };
```

### 4.2 Draw Alignment

```cpp
struct DrawRecord {
    uint32_t eventId = 0;
    std::string drawType;      // "Draw", "DrawIndexed", "Dispatch", "Clear", "Copy"
    std::string markerPath;    // hierarchical marker path, empty if none
    uint64_t triangles = 0;    // (numIndices / 3) * numInstances
    uint32_t instances = 0;
    std::string passName;
    std::string shaderHash;    // fallback matching key
    std::string topology;      // "TriangleList", "TriangleStrip", etc.
};

struct DrawDiffRow {
    DiffStatus status = DiffStatus::Equal;
    std::optional<DrawRecord> a;
    std::optional<DrawRecord> b;
    std::string confidence;    // "high", "medium", "low"
};

struct DrawsDiffResult {
    std::vector<DrawDiffRow> rows;
    int added = 0, deleted = 0, modified = 0, unchanged = 0;
};
```

### 4.3 Resources

```cpp
struct ResourceDiffRow {
    DiffStatus status = DiffStatus::Equal;
    std::string name;
    std::string typeA, typeB;
    std::string confidence;    // "high" (named) or "low" (unnamed positional)
};

struct ResourcesDiffResult {
    std::vector<ResourceDiffRow> rows;
    int added = 0, deleted = 0, modified = 0, unchanged = 0;
};
```

### 4.4 Stats / Passes

```cpp
struct PassDiffRow {
    DiffStatus status = DiffStatus::Equal;
    std::string name;
    std::optional<uint32_t> drawsA, drawsB;
    std::optional<uint64_t> trianglesA, trianglesB;
    std::optional<uint32_t> dispatchesA, dispatchesB;
};

struct StatsDiffResult {
    std::vector<PassDiffRow> rows;
    int passesChanged = 0, passesAdded = 0, passesDeleted = 0;
    int64_t drawsDelta = 0, trianglesDelta = 0, dispatchesDelta = 0;
};
```

### 4.5 Pipeline

```cpp
struct PipeFieldDiff {
    std::string section;    // "blend", "viewport", "rasterizer", etc.
    std::string field;      // "cullMode" or "blends[0].srcBlend"
    std::string valueA;     // stringified
    std::string valueB;
    bool changed = false;
};

struct PipelineDiffResult {
    uint32_t eidA = 0, eidB = 0;
    std::string markerPath;
    std::vector<PipeFieldDiff> fields;
    int changedCount = 0, totalCount = 0;
};
```

### 4.6 Framebuffer

Reuses existing `ImageCompareResult` from types.h:

```cpp
struct ImageCompareResult {
    bool pass = false;
    int diffPixels = 0;
    int totalPixels = 0;
    double diffRatio = 0.0;
    std::string diffOutputPath;
    std::string message;
};
```

### 4.7 Summary

```cpp
struct SummaryRow {
    std::string category;   // "draws", "passes", "resources", "events"
    int valueA = 0, valueB = 0;
    int delta = 0;
};

struct SummaryDiffResult {
    std::vector<SummaryRow> rows;
    bool identical = false;
};
```

---

## 5. Diff Algorithms

### 5.1 Function Signatures

```cpp
namespace renderdoc::core {

SummaryDiffResult    diffSummary(DiffSession& session);
DrawsDiffResult      diffDraws(DiffSession& session);
ResourcesDiffResult  diffResources(DiffSession& session);
StatsDiffResult      diffStats(DiffSession& session);
PipelineDiffResult   diffPipeline(DiffSession& session, const std::string& markerPath);
ImageCompareResult   diffFramebuffer(DiffSession& session,
                                      uint32_t eidA = 0, uint32_t eidB = 0,
                                      int target = 0,
                                      double threshold = 0.0,
                                      const std::string& diffOutput = "");

// Internal: LCS alignment (exposed for unit testing)
using AlignedPair = std::pair<std::optional<size_t>, std::optional<size_t>>;
std::vector<AlignedPair> lcsAlign(const std::vector<std::string>& keysA,
                                   const std::vector<std::string>& keysB);
}
```

### 5.2 LCS Draw Alignment Algorithm

**Match key generation:**

1. **Marker mode** (when any draw has a non-empty markerPath):
   - Key = `markerPath + "|" + drawType + "|" + sequentialIndex`
   - `sequentialIndex` = count of same (markerPath, drawType) pairs seen before this record

2. **Fallback mode** (all markers empty):
   - Key = `drawType + "|" + shaderHash + "|" + topology`

**LCS core:**
- Standard O(n*m) dynamic programming
- Backtrack from bottom-right to build matched pairs
- Unmatched items interleaved as Added/Deleted

**Performance optimization:**
- When total draws > 500 AND markers present: group by top-level marker, run LCS per group, concatenate results

**Status classification:**
- `Equal`: drawType + triangles + instances all match
- `Modified`: aligned but any field differs
- `Added`: present only in B
- `Deleted`: present only in A

### 5.3 Resource Matching

1. **Named resources** (name non-empty): match by `lowercase(name)`, first occurrence only
2. **Unnamed resources**: group by type, match by positional index within group
3. Named matches = "high" confidence, unnamed = "low"

### 5.4 Stats / Passes Matching

- Match passes by `lowercase(strip(name))`
- Compare three metrics: draws, triangles, dispatches
- Equal = all three match, Modified = any differs

### 5.5 Pipeline Comparison

1. Use `diffDraws()` to align draws
2. Find the aligned pair matching the requested `markerPath` (supports `[N]` index syntax)
3. Navigate to respective EIDs on both controllers
4. Extract pipeline state from both controllers via existing `getPipelineState()` core function
5. Compare sections using the fields available in our `PipelineState` struct:
   - **Shaders**: compare bound shader IDs and entry points per stage
   - **Render targets**: compare RT count, formats, dimensions
   - **Depth target**: compare depth target binding
   - **Viewports**: compare viewport dimensions and depth ranges
   - Additionally, query per-API state objects (D3D11Pipe, D3D12Pipe, GLPipe, VKPipe) from RenderDoc's `GetPipelineState()` for extended fields: topology, rasterizer, blend, depth/stencil, MSAA
   - **Flat fields** (topology, rasterizer, depth_stencil, msaa): field-by-field
   - **List fields** (blend states, viewports): element-by-element, then sub-fields
   - **Nested fields** (stencil front/back): recursive field comparison
6. All values stringified for comparison
7. Note: Available pipeline sections depend on the graphics API. Fields not available for a given API are omitted.

### 5.6 Framebuffer Comparison

1. If eidA/eidB == 0, find last draw event in each capture
2. Navigate both controllers to respective EIDs
3. Export render target at specified target index from both controllers to temp PNGs
4. Reuse existing `compareImages()` from assertions module (stb_image based)
5. Return `ImageCompareResult` with diffPixels, diffRatio, optional diff image

### 5.7 Summary

Query counts from both captures:
- `draws`: total draw calls
- `passes`: number of render passes
- `resources`: total GPU resource count
- `events`: total event count

Compare each pair, compute delta, set `identical = true` if all deltas are zero.

---

## 6. MCP Tools

8 new tools (total: 40 → 48):

### 6.1 diff_open

```json
{
  "name": "diff_open",
  "description": "Open two capture files for comparison",
  "inputSchema": {
    "type": "object",
    "properties": {
      "captureA": { "type": "string", "description": "Path to first .rdc file" },
      "captureB": { "type": "string", "description": "Path to second .rdc file" }
    },
    "required": ["captureA", "captureB"]
  }
}
```

Returns: `{ infoA: CaptureInfo, infoB: CaptureInfo }`

### 6.2 diff_close

```json
{
  "name": "diff_close",
  "description": "Close the diff session",
  "inputSchema": { "type": "object", "properties": {} }
}
```

Returns: `{ success: true }`

### 6.3 diff_summary

```json
{
  "name": "diff_summary",
  "description": "Get a high-level summary of differences between two captures",
  "inputSchema": { "type": "object", "properties": {} }
}
```

Returns: `SummaryDiffResult`

### 6.4 diff_draws

```json
{
  "name": "diff_draws",
  "description": "Compare draw call sequences using LCS alignment",
  "inputSchema": { "type": "object", "properties": {} }
}
```

Returns: `DrawsDiffResult`

### 6.5 diff_resources

```json
{
  "name": "diff_resources",
  "description": "Compare GPU resource lists between two captures",
  "inputSchema": { "type": "object", "properties": {} }
}
```

Returns: `ResourcesDiffResult`

### 6.6 diff_stats

```json
{
  "name": "diff_stats",
  "description": "Compare per-pass statistics between two captures",
  "inputSchema": { "type": "object", "properties": {} }
}
```

Returns: `StatsDiffResult`

### 6.7 diff_pipeline

```json
{
  "name": "diff_pipeline",
  "description": "Compare pipeline state at a matched draw call",
  "inputSchema": {
    "type": "object",
    "properties": {
      "marker": { "type": "string", "description": "Draw marker path (e.g. 'GBuffer/Floor[0]')" }
    },
    "required": ["marker"]
  }
}
```

Returns: `PipelineDiffResult`

### 6.8 diff_framebuffer

```json
{
  "name": "diff_framebuffer",
  "description": "Pixel-level comparison of render targets between two captures",
  "inputSchema": {
    "type": "object",
    "properties": {
      "eidA": { "type": "integer", "description": "Event ID in capture A (0 = last draw)" },
      "eidB": { "type": "integer", "description": "Event ID in capture B (0 = last draw)" },
      "target": { "type": "integer", "description": "Color target index (default 0)" },
      "threshold": { "type": "number", "description": "Max diff ratio % to count as identical (default 0.0)" },
      "diffOutput": { "type": "string", "description": "Path to write diff visualization PNG" }
    }
  }
}
```

Returns: `ImageCompareResult`

---

## 7. CLI Commands

Single `diff` subcommand with mode flags:

```
renderdoc-cli diff FILE_A FILE_B [OPTIONS]

Modes (mutually exclusive, default = summary):
  (none)                        Summary mode
  --draws                       Draw call alignment
  --resources                   Resource list comparison
  --stats                       Per-pass statistics
  --pipeline MARKER             Pipeline state at matched draw
  --framebuffer                 Pixel-level render target comparison

Output options:
  --json                        JSON output (default: human-readable text)

Framebuffer options:
  --target N                    Color target index (default 0)
  --threshold F                 Max diff ratio % for "identical" (default 0.0)
  --eid-a N                     Event ID in capture A (default: last draw)
  --eid-b N                     Event ID in capture B (default: last draw)
  --diff-output PATH            Write diff visualization PNG

Pipeline options:
  --verbose                     Show all fields (default: changed only)

Exit codes:
  0  No differences found
  1  Differences detected
  2  Error (file not found, replay init failed, etc.)
```

### CLI Output Formats

**Summary mode (default):**
```
draws:      142 -> 145  (+3)
passes:       5 ->   5  (=)
resources:   48 ->  51  (+3)
events:    1024 -> 1024 (=)
```

**Draws mode:**
```
STATUS  EID_A  EID_B  MARKER           TYPE          TRI_A    TRI_B  CONFIDENCE
=         10     10   GBuffer/Floor    DrawIndexed    1024     1024  high
~         15     15   GBuffer/Wall     DrawIndexed    2048     4096  high
-         20      -   Shadow/Extra     DrawIndexed     512        -  high
+          -     22   Lighting/New     Dispatch          -        -  high
```

**Resources mode:**
```
STATUS  NAME              TYPE_A        TYPE_B
=       MainColorRT       Texture2D     Texture2D
~       ShadowMap         Texture2D     Texture2DMS
+       NewBuffer         -             Buffer
```

**Stats mode:**
```
STATUS  PASS      DRAWS_A  DRAWS_B  DELTA  TRI_A     TRI_B     DELTA
=       GBuffer        12       12     0    24576     24576        0
~       Shadow          8       10    +2     4096      5120    +1024
+       PostFX          -        3    +3        -       768     +768
```

**Pipeline mode:**
```
SECTION      FIELD              A               B
rasterizer   cullMode           Back            Front          <- changed
blend        blends[0].enable   false           true           <- changed
viewport     width              1920            1920
```

**Framebuffer mode:**
```
identical
```
or:
```
diff: 1234/307200 pixels (0.40%)
diff image written to: /tmp/diff.png
```

---

## 8. Error Handling

### New Error Codes

```cpp
enum class Code {
    // ... existing codes ...
    DiffNotOpen,           // diff_* tool called without diff_open
    DiffAlreadyOpen,       // diff_open called when already open
    DiffAlignmentFailed,   // LCS alignment produced no matches
    MarkerNotFound,        // pipeline diff marker path not in either capture
};
```

### Error Scenarios

| Scenario | Error Code | Message |
|----------|-----------|---------|
| diff tool called before diff_open | DiffNotOpen | "No diff session open. Call diff_open first." |
| diff_open when already open | DiffAlreadyOpen | "Diff session already open. Call diff_close first." |
| File A not found | FileNotFound | "Capture file not found: {path}" |
| File B not found | FileNotFound | "Capture file not found: {path}" |
| Replay init fails for A | ReplayInitFailed | "Failed to initialize replay for capture A: {path}" |
| Replay init fails for B | ReplayInitFailed | "Failed to initialize replay for capture B: {path}" |
| Pipeline marker not found | MarkerNotFound | "Marker path not found: {marker}" |
| Framebuffer size mismatch | ImageSizeMismatch | "Render target dimensions differ: {w1}x{h1} vs {w2}x{h2}" |

---

## 9. JSON Serialization

Extend `src/mcp/serialization.h/cpp` with `to_json` for all new types:

- `DiffStatus` → `"equal"`, `"modified"`, `"added"`, `"deleted"`
- `DrawRecord` → object with all fields
- `DrawDiffRow` → `{ status, a?, b?, confidence }`
- `DrawsDiffResult` → `{ rows, added, deleted, modified, unchanged }`
- `ResourceDiffRow` → `{ status, name, typeA, typeB, confidence }`
- `ResourcesDiffResult` → `{ rows, added, deleted, modified, unchanged }`
- `PassDiffRow` → `{ status, name, drawsA?, drawsB?, trianglesA?, trianglesB?, dispatchesA?, dispatchesB? }`
- `StatsDiffResult` → `{ rows, passesChanged, passesAdded, passesDeleted, drawsDelta, trianglesDelta, dispatchesDelta }`
- `PipeFieldDiff` → `{ section, field, valueA, valueB, changed }`
- `PipelineDiffResult` → `{ eidA, eidB, markerPath, fields, changedCount, totalCount }`
- `SummaryRow` → `{ category, valueA, valueB, delta }`
- `SummaryDiffResult` → `{ rows, identical }`
- `DiffSession::OpenResult` → `{ infoA: CaptureInfo, infoB: CaptureInfo }`

---

## 10. Testing

### 10.1 Unit Tests (`test-diff-algo`)

No RenderDoc dependency. Tests the LCS algorithm and match key generation:

- **LCS basic cases:** identical sequences, completely different, single element, empty
- **LCS with markers:** marker-based key generation, sequential index tracking
- **LCS fallback:** shader hash + topology matching when no markers
- **LCS performance:** large sequences (500+ draws) with group optimization
- **Match key edge cases:** empty markers, duplicate markers, special characters

### 10.2 Integration Tests (`test-diff-tools`)

Requires RenderDoc + test fixture:

- **Self-diff (vkcube.rdc vs vkcube.rdc):**
  - `diff_open` succeeds, returns matching CaptureInfo
  - `diff_summary` returns `identical = true`
  - `diff_draws` returns all `Equal` rows
  - `diff_resources` returns all `Equal` rows
  - `diff_stats` returns all `Equal` rows
  - `diff_pipeline` with valid marker returns `changedCount = 0`
  - `diff_framebuffer` returns `diffPixels = 0`
  - `diff_close` succeeds

- **Error handling:**
  - `diff_draws` before `diff_open` → DiffNotOpen error
  - `diff_open` with nonexistent file → FileNotFound error
  - `diff_open` twice → DiffAlreadyOpen error
  - `diff_pipeline` with invalid marker → MarkerNotFound error

### 10.3 Protocol Test Update

Update `test_protocol.cpp` tool count from 40 to 48.

---

## 11. Skill Update

Add to `skills/renderdoc-mcp/SKILL.md`:

### New Workflow: Frame Regression Diagnosis

```
When comparing two captures to find rendering differences:
1. diff_open captureA captureB          → Load both captures
2. diff_summary                          → Quick overview: any differences?
3. diff_draws                            → Which draws changed/added/removed?
4. diff_pipeline "MarkerPath"            → What pipeline state changed at that draw?
5. diff_framebuffer --diff-output path   → Pixel-level visual comparison
6. diff_close                            → Clean up
```

### Tool Reference Additions

Document all 8 new diff tools with parameter descriptions and example outputs.

---

## 12. Implementation Order

Suggested implementation sequence:

1. **Types + DiffSession** — diff.h types, diff_session.h/cpp
2. **LCS algorithm** — lcsAlign() + unit tests
3. **Core diff functions** — diffSummary, diffDraws, diffResources, diffStats, diffPipeline, diffFramebuffer
4. **Serialization** — to_json for all diff types
5. **MCP tools** — 8 tool registrations in diff_tools.cpp
6. **CLI commands** — diff subcommand in main.cpp
7. **Integration tests** — self-diff + error cases
8. **Skill update** — SKILL.md + README
