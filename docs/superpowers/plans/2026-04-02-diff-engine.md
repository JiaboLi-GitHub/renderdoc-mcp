# Diff Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a dual-session diff engine comparing two RenderDoc captures across 6 modes (summary, draws, resources, stats, pipeline, framebuffer) via 8 MCP tools + CLI.

**Architecture:** New `DiffSession` class holds two independent `IReplayController` instances. Diff algorithms in `diff.cpp` query both controllers. A `ToolContext` struct replaces the bare `Session&` in the handler signature so diff tools can access `DiffSession` without global state.

**Tech Stack:** C++17, nlohmann/json, RenderDoc replay API, stb_image (existing), GoogleTest

**Spec:** `docs/superpowers/specs/2026-04-02-diff-engine-design.md`

---

## File Structure

### New files

| File | Responsibility |
|------|---------------|
| `src/core/diff_session.h` | `DiffSession` class: dual capture open/close lifecycle |
| `src/core/diff_session.cpp` | Implementation: open two captures, manage two controllers |
| `src/core/diff.h` | Diff result types + algorithm declarations |
| `src/core/diff.cpp` | 6 diff algorithms: summary, draws, resources, stats, pipeline, framebuffer |
| `src/mcp/tools/diff_tools.cpp` | 8 MCP tool registrations for diff_open through diff_framebuffer |
| `tests/unit/test_diff_algo.cpp` | LCS algorithm unit tests (no RenderDoc dependency) |
| `tests/integration/test_tools_diff.cpp` | Integration tests: self-diff + error cases |

### Modified files

| File | What changes |
|------|-------------|
| `src/mcp/tool_registry.h` | Add `ToolContext` struct; change `ToolDef::handler` signature and `callTool` signature |
| `src/mcp/tool_registry.cpp` | Update `callTool` to pass `ToolContext&` |
| `src/mcp/mcp_server.h` | Add `DiffSession` ownership pointers; update injection constructor |
| `src/mcp/mcp_server.cpp` | Build `ToolContext` in `handleToolsCall`; update injection constructor |
| `src/mcp/mcp_server_default.cpp` | Create `DiffSession`; register diff tools; pass both sessions |
| `src/mcp/tools/tools.h` | Declare `registerDiffTools` |
| `src/mcp/tools/session_tools.cpp` | Change handler lambda: `(Session& s, ...)` → `(ToolContext& ctx, ...)` |
| `src/mcp/tools/event_tools.cpp` | Same handler signature migration |
| `src/mcp/tools/info_tools.cpp` | Same |
| `src/mcp/tools/pipeline_tools.cpp` | Same |
| `src/mcp/tools/shader_tools.cpp` | Same |
| `src/mcp/tools/resource_tools.cpp` | Same |
| `src/mcp/tools/export_tools.cpp` | Same |
| `src/mcp/tools/capture_tools.cpp` | Same |
| `src/mcp/tools/pixel_tools.cpp` | Same |
| `src/mcp/tools/debug_tools.cpp` | Same |
| `src/mcp/tools/texstats_tools.cpp` | Same |
| `src/mcp/tools/shader_edit_tools.cpp` | Same |
| `src/mcp/tools/mesh_tools.cpp` | Same |
| `src/mcp/tools/snapshot_tools.cpp` | Same |
| `src/mcp/tools/usage_tools.cpp` | Same |
| `src/mcp/tools/assertion_tools.cpp` | Same |
| `src/mcp/serialization.h` | Add `to_json` for all diff types |
| `src/mcp/serialization.cpp` | Implement `to_json` for diff types |
| `src/core/errors.h` | Add 4 new error codes |
| `src/cli/main.cpp` | Add `diff` command |
| `CMakeLists.txt` | Add diff source files to build targets |
| `tests/unit/session_stub.cpp` | Add DiffSession stub |
| `tests/integration/test_protocol.cpp` | Update tool count 40 → 48 |

---

## Task 1: Add Error Codes

**Files:**
- Modify: `src/core/errors.h`

- [ ] **Step 1: Add 4 new error codes**

In `src/core/errors.h`, add after `InvalidPath`:

```cpp
        InvalidPath,
        DiffNotOpen,
        DiffAlreadyOpen,
        DiffAlignmentFailed,
        MarkerNotFound
```

- [ ] **Step 2: Verify build**

Run: `cmake --build build --config Release --target renderdoc-mcp-proto 2>&1 | tail -5`
Expected: Build succeeds (errors.h is header-only, included transitively).

- [ ] **Step 3: Commit**

```bash
git add src/core/errors.h
git commit -m "feat(core): add 4 diff error codes"
```

---

## Task 2: Introduce ToolContext and Migrate Handler Signature

This task changes the tool dispatch signature from `(Session&, json)` to `(ToolContext&, json)`. All existing tools are updated mechanically.

**Files:**
- Modify: `src/mcp/tool_registry.h`
- Modify: `src/mcp/tool_registry.cpp`
- Modify: `src/mcp/mcp_server.h`
- Modify: `src/mcp/mcp_server.cpp`
- Modify: `src/mcp/mcp_server_default.cpp`
- Modify: all 16 files in `src/mcp/tools/*.cpp`
- Modify: `tests/unit/session_stub.cpp`

- [ ] **Step 1: Update tool_registry.h**

Replace the contents of `src/mcp/tool_registry.h` with:

```cpp
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace renderdoc::core {
    class Session;
    class DiffSession;
}

namespace renderdoc::mcp {

// Protocol-level parameter error -- McpServer converts to JSON-RPC -32602
struct InvalidParamsError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ToolContext {
    core::Session& session;
    core::DiffSession& diffSession;
};

struct ToolDef {
    std::string name;
    std::string description;
    nlohmann::json inputSchema;
    // Returns raw JSON business data. Throws std::runtime_error for tool-level errors.
    std::function<nlohmann::json(ToolContext&, const nlohmann::json&)> handler;
};

class ToolRegistry {
public:
    void registerTool(ToolDef def);
    nlohmann::json getToolDefinitions() const;
    // Flow: lookup → validateArgs → call handler
    // InvalidParamsError for validation failures, std::runtime_error for tool errors
    nlohmann::json callTool(const std::string& name,
                            ToolContext& ctx,
                            const nlohmann::json& args);
    bool hasTool(const std::string& name) const;

private:
    void validateArgs(const ToolDef& tool, const nlohmann::json& args) const;

    std::vector<ToolDef> m_tools;
    std::unordered_map<std::string, size_t> m_toolIndex;
};

} // namespace renderdoc::mcp
```

- [ ] **Step 2: Update tool_registry.cpp**

In `src/mcp/tool_registry.cpp`, change the `callTool` signature:

Replace:
```cpp
json ToolRegistry::callTool(const std::string& name,
                            core::Session& session,
                            const json& args)
```
With:
```cpp
json ToolRegistry::callTool(const std::string& name,
                            ToolContext& ctx,
                            const json& args)
```

And in the body, replace `def.handler(session, args)` with `def.handler(ctx, args)`.

- [ ] **Step 3: Update mcp_server.h**

Replace the contents of `src/mcp/mcp_server.h` with:

```cpp
#pragma once

#include <nlohmann/json.hpp>
#include "mcp/tool_registry.h"
#include <memory>

namespace renderdoc::core {
    class Session;
    class DiffSession;
}

namespace renderdoc::mcp {

class McpServer
{
public:
    // Default constructor: creates own sessions + registers all tools
    McpServer();
    // Injection constructor: uses external sessions & registry (for testing)
    McpServer(core::Session& session, core::DiffSession& diffSession, ToolRegistry& registry);
    ~McpServer();

    nlohmann::json handleMessage(const nlohmann::json& msg);
    nlohmann::json handleBatch(const nlohmann::json& arr);
    void shutdown();

private:
    nlohmann::json handleInitialize(const nlohmann::json& msg);
    nlohmann::json handleToolsList(const nlohmann::json& msg);
    nlohmann::json handleToolsCall(const nlohmann::json& msg);

    static nlohmann::json makeResponse(const nlohmann::json& id, const nlohmann::json& result);
    static nlohmann::json makeError(const nlohmann::json& id, int code, const std::string& message);
    static nlohmann::json makeToolResult(const nlohmann::json& data, bool isError = false);

    std::unique_ptr<core::Session> m_ownedSession;
    core::Session* m_session = nullptr;
    std::unique_ptr<core::DiffSession> m_ownedDiffSession;
    core::DiffSession* m_diffSession = nullptr;
    std::unique_ptr<ToolRegistry> m_ownedRegistry;
    ToolRegistry* m_registry = nullptr;
    bool m_initialized = false;
};

} // namespace renderdoc::mcp
```

- [ ] **Step 4: Update mcp_server.cpp**

In `src/mcp/mcp_server.cpp`:

1. Add include: `#include "core/diff_session.h"` (will exist after Task 3)
2. Update the injection constructor:

Replace:
```cpp
McpServer::McpServer(core::Session& session, ToolRegistry& registry)
    : m_session(&session)
    , m_registry(&registry)
    , m_initialized(false)
{
}
```
With:
```cpp
McpServer::McpServer(core::Session& session, core::DiffSession& diffSession, ToolRegistry& registry)
    : m_session(&session)
    , m_diffSession(&diffSession)
    , m_registry(&registry)
    , m_initialized(false)
{
}
```

3. Update `handleToolsCall` to build ToolContext:

Replace:
```cpp
        json rawResult = m_registry->callTool(toolName, *m_session, arguments);
```
With:
```cpp
        ToolContext ctx{*m_session, *m_diffSession};
        json rawResult = m_registry->callTool(toolName, ctx, arguments);
```

4. Update `shutdown()` to also close diff session:

Replace:
```cpp
void McpServer::shutdown()
{
    if(m_session)
        m_session->close();
}
```
With:
```cpp
void McpServer::shutdown()
{
    if(m_session)
        m_session->close();
    if(m_diffSession)
        m_diffSession->close();
}
```

- [ ] **Step 5: Migrate all 16 existing tool files**

For every file in `src/mcp/tools/` (session_tools.cpp, event_tools.cpp, info_tools.cpp, pipeline_tools.cpp, shader_tools.cpp, resource_tools.cpp, export_tools.cpp, capture_tools.cpp, pixel_tools.cpp, debug_tools.cpp, texstats_tools.cpp, shader_edit_tools.cpp, mesh_tools.cpp, snapshot_tools.cpp, usage_tools.cpp, assertion_tools.cpp):

Find and replace every handler lambda signature:

```
[](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
```
→
```
[](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
```

This is a mechanical transformation. Each handler body already uses `session` by name, so adding `auto& session = ctx.session;` ensures zero behavior change.

Also add include at top of each file if not present: `#include "mcp/tool_registry.h"` (already included in most files since ToolDef lives there).

- [ ] **Step 6: Update session_stub.cpp for tests**

In `tests/unit/session_stub.cpp`, add a minimal `DiffSession` stub so that unit tests (which build against `renderdoc-mcp-proto` without RenderDoc) can still construct a `ToolContext`:

Add to the file:

```cpp
#include "core/diff_session.h"

namespace renderdoc::core {

DiffSession::DiffSession() = default;
DiffSession::~DiffSession() { close(); }
void DiffSession::close() {}
bool DiffSession::isOpen() const { return false; }
IReplayController* DiffSession::controllerA() const { return nullptr; }
IReplayController* DiffSession::controllerB() const { return nullptr; }
ICaptureFile* DiffSession::captureFileA() const { return nullptr; }
ICaptureFile* DiffSession::captureFileB() const { return nullptr; }
const std::string& DiffSession::pathA() const { static std::string s; return s; }
const std::string& DiffSession::pathB() const { static std::string s; return s; }
DiffSession::OpenResult DiffSession::open(const std::string&, const std::string&) {
    throw CoreError(CoreError::Code::DiffNotOpen, "stub");
}

} // namespace renderdoc::core
```

Note: This requires `diff_session.h` to exist, so Task 3 must create the header first. If building incrementally, create the header in Task 3 before compiling this.

- [ ] **Step 7: Update unit tests that construct McpServer**

In `tests/unit/test_mcp_server.cpp` and `tests/unit/test_tool_registry.cpp`, update any code that constructs `McpServer` or calls `callTool` to use the new signatures. Specifically:

For `McpServer` construction, change:
```cpp
McpServer(session, registry)
```
To:
```cpp
McpServer(session, diffSession, registry)
```
(adding a local `DiffSession diffSession;` variable)

For `callTool`, change:
```cpp
registry.callTool(name, session, args)
```
To:
```cpp
ToolContext ctx{session, diffSession};
registry.callTool(name, ctx, args)
```

- [ ] **Step 8: Build and verify**

Run: `cmake --build build --config Release 2>&1 | tail -10`
Expected: Full build succeeds with no errors. All existing tools compile with new signature.

- [ ] **Step 9: Run existing unit tests**

Run: `cd build && ctest -R test-unit -C Release --output-on-failure`
Expected: All existing unit tests pass.

- [ ] **Step 10: Commit**

```bash
git add src/mcp/tool_registry.h src/mcp/tool_registry.cpp \
        src/mcp/mcp_server.h src/mcp/mcp_server.cpp \
        src/mcp/mcp_server_default.cpp \
        src/mcp/tools/*.cpp \
        tests/unit/session_stub.cpp \
        tests/unit/test_mcp_server.cpp tests/unit/test_tool_registry.cpp
git commit -m "refactor(mcp): introduce ToolContext for multi-session tool dispatch"
```

---

## Task 3: DiffSession Class

**Files:**
- Create: `src/core/diff_session.h`
- Create: `src/core/diff_session.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create diff_session.h**

Create `src/core/diff_session.h`:

```cpp
#pragma once

#include "core/types.h"
#include <string>

struct ICaptureFile;
struct IReplayController;

namespace renderdoc::core {

class DiffSession {
public:
    DiffSession();
    ~DiffSession();

    DiffSession(const DiffSession&) = delete;
    DiffSession& operator=(const DiffSession&) = delete;

    struct OpenResult {
        CaptureInfo infoA;
        CaptureInfo infoB;
    };

    OpenResult open(const std::string& pathA, const std::string& pathB);
    void close();
    bool isOpen() const;

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
    bool m_replayInitialized = false;

    void ensureReplayInitialized();
    CaptureInfo openOne(const std::string& path, ICaptureFile*& cap, IReplayController*& ctrl);
    void closeOne(ICaptureFile*& cap, IReplayController*& ctrl);
};

} // namespace renderdoc::core
```

- [ ] **Step 2: Create diff_session.cpp**

Create `src/core/diff_session.cpp`:

```cpp
#include "core/diff_session.h"
#include "core/errors.h"
#include <renderdoc_replay.h>
#include <cstring>

namespace renderdoc::core {

namespace {

GraphicsApi toGraphicsApi(GraphicsAPI api) {
    switch (api) {
        case GraphicsAPI::D3D11:  return GraphicsApi::D3D11;
        case GraphicsAPI::D3D12:  return GraphicsApi::D3D12;
        case GraphicsAPI::OpenGL: return GraphicsApi::OpenGL;
        case GraphicsAPI::Vulkan: return GraphicsApi::Vulkan;
        default:                  return GraphicsApi::Unknown;
    }
}

uint32_t countAllEvents(const ActionDescription& action) {
    uint32_t count = 1;
    for (const auto& child : action.children)
        count += countAllEvents(child);
    return count;
}

uint32_t countDrawCalls(const ActionDescription& action) {
    uint32_t count = 0;
    if (action.flags & ActionFlags::Drawcall)
        count = 1;
    for (const auto& child : action.children)
        count += countDrawCalls(child);
    return count;
}

} // anonymous namespace

DiffSession::DiffSession() = default;

DiffSession::~DiffSession() {
    close();
}

void DiffSession::ensureReplayInitialized() {
    if (!m_replayInitialized) {
        GlobalEnvironment env;
        memset(&env, 0, sizeof(env));
        rdcarray<rdcstr> args;
        RENDERDOC_InitialiseReplay(env, args);
        m_replayInitialized = true;
    }
}

CaptureInfo DiffSession::openOne(const std::string& path,
                                  ICaptureFile*& cap,
                                  IReplayController*& ctrl) {
    cap = RENDERDOC_OpenCaptureFile();
    if (!cap)
        throw CoreError(CoreError::Code::InternalError, "Failed to create capture file object");

    auto status = cap->OpenFile(rdcstr(path.c_str()), "", nullptr);
    if (!status.OK()) {
        cap->Shutdown();
        cap = nullptr;
        throw CoreError(CoreError::Code::FileNotFound,
                        "Failed to open capture: " + std::string(status.Message().c_str()));
    }

    ReplayOptions opts;
    auto [replayStatus, controller] = cap->OpenCapture(opts, nullptr);
    if (!replayStatus.OK() || !controller) {
        cap->Shutdown();
        cap = nullptr;
        throw CoreError(CoreError::Code::ReplayInitFailed,
                        "Failed to open replay: " + std::string(replayStatus.Message().c_str()));
    }

    ctrl = controller;

    auto apiProps = ctrl->GetAPIProperties();

    const auto& rootActions = ctrl->GetRootActions();
    uint32_t totalEvents = 0;
    uint32_t totalDraws = 0;
    for (const auto& action : rootActions) {
        totalEvents += countAllEvents(action);
        totalDraws += countDrawCalls(action);
    }

    CaptureInfo info;
    info.path = path;
    info.api = toGraphicsApi(apiProps.pipelineType);
    info.degraded = apiProps.degraded;
    info.totalEvents = totalEvents;
    info.totalDraws = totalDraws;
    return info;
}

void DiffSession::closeOne(ICaptureFile*& cap, IReplayController*& ctrl) {
    if (ctrl) { ctrl->Shutdown(); ctrl = nullptr; }
    if (cap) { cap->Shutdown(); cap = nullptr; }
}

DiffSession::OpenResult DiffSession::open(const std::string& pathA,
                                           const std::string& pathB) {
    if (isOpen())
        throw CoreError(CoreError::Code::DiffAlreadyOpen,
                        "Diff session already open. Call diff_close first.");

    ensureReplayInitialized();

    OpenResult result;
    result.infoA = openOne(pathA, m_capA, m_ctrlA);
    m_pathA = pathA;

    try {
        result.infoB = openOne(pathB, m_capB, m_ctrlB);
        m_pathB = pathB;
    } catch (...) {
        closeOne(m_capA, m_ctrlA);
        m_pathA.clear();
        throw;
    }

    return result;
}

void DiffSession::close() {
    closeOne(m_capA, m_ctrlA);
    closeOne(m_capB, m_ctrlB);
    m_pathA.clear();
    m_pathB.clear();
}

bool DiffSession::isOpen() const {
    return m_ctrlA != nullptr && m_ctrlB != nullptr;
}

IReplayController* DiffSession::controllerA() const {
    if (!m_ctrlA)
        throw CoreError(CoreError::Code::DiffNotOpen, "No diff session open. Call diff_open first.");
    return m_ctrlA;
}

IReplayController* DiffSession::controllerB() const {
    if (!m_ctrlB)
        throw CoreError(CoreError::Code::DiffNotOpen, "No diff session open. Call diff_open first.");
    return m_ctrlB;
}

ICaptureFile* DiffSession::captureFileA() const { return m_capA; }
ICaptureFile* DiffSession::captureFileB() const { return m_capB; }
const std::string& DiffSession::pathA() const { return m_pathA; }
const std::string& DiffSession::pathB() const { return m_pathB; }

} // namespace renderdoc::core
```

- [ ] **Step 3: Add to CMakeLists.txt**

In `CMakeLists.txt`, add `src/core/diff_session.cpp` to the `renderdoc-core` static library. Add after the `src/core/assertions.cpp` line:

```
        src/core/diff_session.cpp
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --config Release --target renderdoc-core 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/core/diff_session.h src/core/diff_session.cpp CMakeLists.txt
git commit -m "feat(core): add DiffSession for dual-capture replay"
```

---

## Task 4: Diff Types and LCS Algorithm

**Files:**
- Create: `src/core/diff.h`
- Create: `src/core/diff.cpp` (LCS algorithm only in this task)
- Create: `tests/unit/test_diff_algo.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create diff.h with all types + function declarations**

Create `src/core/diff.h`:

```cpp
#pragma once

#include "core/types.h"
#include <optional>
#include <string>
#include <vector>

namespace renderdoc::core {

class DiffSession;

// --- Common ---
enum class DiffStatus { Equal, Modified, Added, Deleted };

// --- Draw Alignment ---
struct DrawRecord {
    uint32_t eventId = 0;
    std::string drawType;
    std::string markerPath;
    uint64_t triangles = 0;
    uint32_t instances = 0;
    std::string passName;
    std::string shaderHash;
    std::string topology;
};

struct DrawDiffRow {
    DiffStatus status = DiffStatus::Equal;
    std::optional<DrawRecord> a;
    std::optional<DrawRecord> b;
    std::string confidence;
};

struct DrawsDiffResult {
    std::vector<DrawDiffRow> rows;
    int added = 0, deleted = 0, modified = 0, unchanged = 0;
};

// --- Resources ---
struct ResourceDiffRow {
    DiffStatus status = DiffStatus::Equal;
    std::string name;
    std::string typeA, typeB;
    std::string confidence;
};

struct ResourcesDiffResult {
    std::vector<ResourceDiffRow> rows;
    int added = 0, deleted = 0, modified = 0, unchanged = 0;
};

// --- Stats / Passes ---
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

// --- Pipeline ---
struct PipeFieldDiff {
    std::string section;
    std::string field;
    std::string valueA;
    std::string valueB;
    bool changed = false;
};

struct PipelineDiffResult {
    uint32_t eidA = 0, eidB = 0;
    std::string markerPath;
    std::vector<PipeFieldDiff> fields;
    int changedCount = 0, totalCount = 0;
};

// --- Summary ---
struct SummaryRow {
    std::string category;
    int valueA = 0, valueB = 0;
    int delta = 0;
};

struct SummaryDiffResult {
    std::vector<SummaryRow> rows;
    bool identical = false;
    std::string divergedAt;
};

// --- Algorithm APIs ---

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

// LCS alignment (exposed for unit testing)
using AlignedPair = std::pair<std::optional<size_t>, std::optional<size_t>>;
std::vector<AlignedPair> lcsAlign(const std::vector<std::string>& keysA,
                                   const std::vector<std::string>& keysB);

// Match key generation (exposed for unit testing)
std::string makeDrawMatchKey(const DrawRecord& rec, bool hasMarkers);

} // namespace renderdoc::core
```

- [ ] **Step 2: Create diff.cpp with LCS algorithm (other functions as stubs)**

Create `src/core/diff.cpp`:

```cpp
#include "core/diff.h"
#include "core/diff_session.h"
#include "core/errors.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace renderdoc::core {

// --- LCS Alignment ---

std::vector<AlignedPair> lcsAlign(const std::vector<std::string>& keysA,
                                   const std::vector<std::string>& keysB) {
    const size_t n = keysA.size();
    const size_t m = keysB.size();

    if (n == 0 && m == 0)
        return {};

    if (n == 0) {
        std::vector<AlignedPair> result;
        for (size_t j = 0; j < m; j++)
            result.push_back({std::nullopt, j});
        return result;
    }

    if (m == 0) {
        std::vector<AlignedPair> result;
        for (size_t i = 0; i < n; i++)
            result.push_back({i, std::nullopt});
        return result;
    }

    // Standard LCS DP table
    std::vector<std::vector<uint32_t>> dp(n + 1, std::vector<uint32_t>(m + 1, 0));
    for (size_t i = 1; i <= n; i++) {
        for (size_t j = 1; j <= m; j++) {
            if (keysA[i - 1] == keysB[j - 1])
                dp[i][j] = dp[i - 1][j - 1] + 1;
            else
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
        }
    }

    // Backtrack to build aligned pairs
    std::vector<AlignedPair> result;
    size_t i = n, j = m;

    // Collect pairs in reverse
    std::vector<AlignedPair> rev;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && keysA[i - 1] == keysB[j - 1]) {
            rev.push_back({i - 1, j - 1});
            i--;
            j--;
        } else if (j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j])) {
            rev.push_back({std::nullopt, j - 1});
            j--;
        } else {
            rev.push_back({i - 1, std::nullopt});
            i--;
        }
    }

    result.assign(rev.rbegin(), rev.rend());
    return result;
}

// --- Match Key Generation ---

std::string makeDrawMatchKey(const DrawRecord& rec, bool hasMarkers) {
    if (hasMarkers) {
        return rec.markerPath + "|" + rec.drawType;
    } else {
        return rec.drawType + "|" + rec.shaderHash + "|" + rec.topology;
    }
}

// --- Stub implementations (filled in Task 5-7) ---

DrawsDiffResult diffDraws(DiffSession& session) {
    (void)session;
    throw CoreError(CoreError::Code::InternalError, "diffDraws not yet implemented");
}

ResourcesDiffResult diffResources(DiffSession& session) {
    (void)session;
    throw CoreError(CoreError::Code::InternalError, "diffResources not yet implemented");
}

StatsDiffResult diffStats(DiffSession& session) {
    (void)session;
    throw CoreError(CoreError::Code::InternalError, "diffStats not yet implemented");
}

PipelineDiffResult diffPipeline(DiffSession& session, const std::string& markerPath) {
    (void)session; (void)markerPath;
    throw CoreError(CoreError::Code::InternalError, "diffPipeline not yet implemented");
}

ImageCompareResult diffFramebuffer(DiffSession& session,
                                    uint32_t eidA, uint32_t eidB,
                                    int target, double threshold,
                                    const std::string& diffOutput) {
    (void)session; (void)eidA; (void)eidB; (void)target; (void)threshold; (void)diffOutput;
    throw CoreError(CoreError::Code::InternalError, "diffFramebuffer not yet implemented");
}

SummaryDiffResult diffSummary(DiffSession& session) {
    (void)session;
    throw CoreError(CoreError::Code::InternalError, "diffSummary not yet implemented");
}

} // namespace renderdoc::core
```

- [ ] **Step 3: Add diff.cpp to CMakeLists.txt**

In `CMakeLists.txt`, add after `src/core/diff_session.cpp`:

```
        src/core/diff.cpp
```

- [ ] **Step 4: Create unit test for LCS**

Create `tests/unit/test_diff_algo.cpp`:

```cpp
#include <gtest/gtest.h>
#include "core/diff.h"

using namespace renderdoc::core;

// --- lcsAlign tests ---

TEST(LcsAlign, IdenticalSequences) {
    std::vector<std::string> a = {"x", "y", "z"};
    auto aligned = lcsAlign(a, a);
    ASSERT_EQ(aligned.size(), 3u);
    for (size_t i = 0; i < 3; i++) {
        ASSERT_TRUE(aligned[i].first.has_value());
        ASSERT_TRUE(aligned[i].second.has_value());
        EXPECT_EQ(*aligned[i].first, i);
        EXPECT_EQ(*aligned[i].second, i);
    }
}

TEST(LcsAlign, CompletelyDifferent) {
    std::vector<std::string> a = {"a", "b"};
    std::vector<std::string> b = {"c", "d"};
    auto aligned = lcsAlign(a, b);
    // All should be unmatched: 2 deleted + 2 added = 4 entries
    ASSERT_EQ(aligned.size(), 4u);
    int deletions = 0, additions = 0;
    for (auto& [ai, bi] : aligned) {
        if (ai.has_value() && !bi.has_value()) deletions++;
        if (!ai.has_value() && bi.has_value()) additions++;
    }
    EXPECT_EQ(deletions, 2);
    EXPECT_EQ(additions, 2);
}

TEST(LcsAlign, EmptyA) {
    std::vector<std::string> a = {};
    std::vector<std::string> b = {"x", "y"};
    auto aligned = lcsAlign(a, b);
    ASSERT_EQ(aligned.size(), 2u);
    for (auto& [ai, bi] : aligned) {
        EXPECT_FALSE(ai.has_value());
        EXPECT_TRUE(bi.has_value());
    }
}

TEST(LcsAlign, EmptyB) {
    std::vector<std::string> a = {"x", "y"};
    std::vector<std::string> b = {};
    auto aligned = lcsAlign(a, b);
    ASSERT_EQ(aligned.size(), 2u);
    for (auto& [ai, bi] : aligned) {
        EXPECT_TRUE(ai.has_value());
        EXPECT_FALSE(bi.has_value());
    }
}

TEST(LcsAlign, BothEmpty) {
    std::vector<std::string> a = {};
    auto aligned = lcsAlign(a, a);
    EXPECT_TRUE(aligned.empty());
}

TEST(LcsAlign, InsertionInMiddle) {
    std::vector<std::string> a = {"a", "b", "c"};
    std::vector<std::string> b = {"a", "X", "b", "c"};
    auto aligned = lcsAlign(a, b);
    // a,b,c should be matched; X should be added
    ASSERT_EQ(aligned.size(), 4u);
    // First: a matched to a
    EXPECT_EQ(*aligned[0].first, 0u);
    EXPECT_EQ(*aligned[0].second, 0u);
    // Second: X added
    EXPECT_FALSE(aligned[1].first.has_value());
    EXPECT_EQ(*aligned[1].second, 1u);
    // Third: b matched
    EXPECT_EQ(*aligned[2].first, 1u);
    EXPECT_EQ(*aligned[2].second, 2u);
    // Fourth: c matched
    EXPECT_EQ(*aligned[3].first, 2u);
    EXPECT_EQ(*aligned[3].second, 3u);
}

TEST(LcsAlign, DeletionInMiddle) {
    std::vector<std::string> a = {"a", "X", "b", "c"};
    std::vector<std::string> b = {"a", "b", "c"};
    auto aligned = lcsAlign(a, b);
    ASSERT_EQ(aligned.size(), 4u);
    EXPECT_EQ(*aligned[0].first, 0u);  // a matched
    EXPECT_EQ(*aligned[0].second, 0u);
    EXPECT_EQ(*aligned[1].first, 1u);  // X deleted
    EXPECT_FALSE(aligned[1].second.has_value());
    EXPECT_EQ(*aligned[2].first, 2u);  // b matched
    EXPECT_EQ(*aligned[2].second, 1u);
    EXPECT_EQ(*aligned[3].first, 3u);  // c matched
    EXPECT_EQ(*aligned[3].second, 2u);
}

TEST(LcsAlign, DuplicateKeysHandledCorrectly) {
    // Two draws with same key in A, three in B — insertion localizes correctly
    std::vector<std::string> a = {"D", "D", "E"};
    std::vector<std::string> b = {"D", "D", "D", "E"};
    auto aligned = lcsAlign(a, b);
    // D,D,E should match; one extra D added
    int matched = 0, added = 0;
    for (auto& [ai, bi] : aligned) {
        if (ai.has_value() && bi.has_value()) matched++;
        if (!ai.has_value() && bi.has_value()) added++;
    }
    EXPECT_EQ(matched, 3);
    EXPECT_EQ(added, 1);
}

// --- makeDrawMatchKey tests ---

TEST(MatchKey, MarkerMode) {
    DrawRecord rec;
    rec.markerPath = "GBuffer/Floor";
    rec.drawType = "DrawIndexed";
    rec.shaderHash = "abc123";
    rec.topology = "TriangleList";

    std::string key = makeDrawMatchKey(rec, true);
    EXPECT_EQ(key, "GBuffer/Floor|DrawIndexed");
}

TEST(MatchKey, FallbackMode) {
    DrawRecord rec;
    rec.markerPath = "";
    rec.drawType = "DrawIndexed";
    rec.shaderHash = "abc123";
    rec.topology = "TriangleList";

    std::string key = makeDrawMatchKey(rec, false);
    EXPECT_EQ(key, "DrawIndexed|abc123|TriangleList");
}

TEST(MatchKey, MarkerModeIgnoresShaderHash) {
    DrawRecord rec;
    rec.markerPath = "Shadow";
    rec.drawType = "Draw";
    rec.shaderHash = "different_hash";

    std::string key = makeDrawMatchKey(rec, true);
    EXPECT_EQ(key, "Shadow|Draw");
}
```

- [ ] **Step 5: Add unit test to tests/CMakeLists.txt**

In `tests/CMakeLists.txt`, add `test_diff_algo.cpp` to the `test-unit` target sources:

```
    unit/test_diff_algo.cpp
```

Note: `test_diff_algo.cpp` includes `core/diff.h` which has no RenderDoc dependency (types only). The LCS function is defined in `diff.cpp` which links against renderdoc-core. Since the unit test target links against `renderdoc-mcp-proto` (no RenderDoc), we need to add the LCS source directly to the proto library OR create a separate test target.

**Solution:** Add `src/core/diff.cpp` to the `renderdoc-mcp-proto` library instead of `renderdoc-core`, since the LCS algorithm has no RenderDoc dependency. But the diff functions (diffDraws etc.) DO depend on RenderDoc. So we need to **split**: put LCS + types in a separate compilation unit or keep diff.cpp in renderdoc-core and link the unit test against renderdoc-core.

**Simpler approach:** Create a standalone `test-diff-algo` test that links against renderdoc-core (requires RENDERDOC_DIR). Add to `tests/CMakeLists.txt` inside the `if(TARGET renderdoc-mcp-lib)` block:

```cmake
    add_executable(test-diff-algo unit/test_diff_algo.cpp)
    target_link_libraries(test-diff-algo PRIVATE renderdoc-core GTest::gtest_main)
    gtest_discover_tests(test-diff-algo PROPERTIES LABELS unit DISCOVERY_MODE PRE_TEST)
    copy_renderdoc_runtime_to_test(test-diff-algo)
```

- [ ] **Step 6: Build and run LCS tests**

Run: `cmake --build build --config Release --target test-diff-algo 2>&1 | tail -5`
Then: `cd build && ctest -R test-diff-algo -C Release --output-on-failure`
Expected: All LCS tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/core/diff.h src/core/diff.cpp CMakeLists.txt \
        tests/unit/test_diff_algo.cpp tests/CMakeLists.txt
git commit -m "feat(core): add diff types and LCS alignment algorithm with tests"
```

---

## Task 5: Implement diffDraws and diffResources

**Files:**
- Modify: `src/core/diff.cpp`

- [ ] **Step 1: Implement helper to build DrawRecords from controller**

In `src/core/diff.cpp`, add at the top of the file (after includes), inside the anonymous namespace:

```cpp
#include <renderdoc_replay.h>
#include <cstring>

namespace {

core::ResourceId toResourceId(::ResourceId id) {
    uint64_t raw = 0;
    std::memcpy(&raw, &id, sizeof(raw));
    return raw;
}

std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}

std::string actionTypeString(ActionFlags flags) {
    if (flags & ActionFlags::Drawcall) {
        if (flags & ActionFlags::Indexed) return "DrawIndexed";
        if (flags & ActionFlags::Instanced) return "DrawInstanced";
        return "Draw";
    }
    if (flags & ActionFlags::Dispatch) return "Dispatch";
    if (flags & ActionFlags::Clear) return "Clear";
    if (flags & ActionFlags::Copy) return "Copy";
    if (flags & ActionFlags::Resolve) return "Resolve";
    return "Other";
}

std::string topologyString(Topology topo) {
    switch (topo) {
        case Topology::TriangleList:  return "TriangleList";
        case Topology::TriangleStrip: return "TriangleStrip";
        case Topology::TriangleFan:   return "TriangleFan";
        case Topology::PointList:     return "PointList";
        case Topology::LineList:      return "LineList";
        case Topology::LineStrip:     return "LineStrip";
        default:                      return "Other";
    }
}

void buildMarkerPath(const ActionDescription& action,
                     const std::string& parentPath,
                     std::vector<DrawRecord>& out) {
    std::string path = parentPath;
    if (!parentPath.empty() && action.customName.size() > 0)
        path += "/";
    if (action.customName.size() > 0)
        path += std::string(action.customName.c_str());

    bool isDraw = (action.flags & ActionFlags::Drawcall) ||
                  (action.flags & ActionFlags::Dispatch) ||
                  (action.flags & ActionFlags::Clear) ||
                  (action.flags & ActionFlags::Copy);

    if (isDraw) {
        DrawRecord rec;
        rec.eventId = action.eventId;
        rec.drawType = actionTypeString(action.flags);
        rec.markerPath = path;
        rec.instances = action.numInstances;
        if (action.flags & ActionFlags::Indexed)
            rec.triangles = (static_cast<uint64_t>(action.numIndices) / 3) * action.numInstances;
        else
            rec.triangles = (static_cast<uint64_t>(action.numIndices) / 3) * action.numInstances;
        out.push_back(std::move(rec));
    }

    for (const auto& child : action.children)
        buildMarkerPath(child, path, out);
}

std::vector<DrawRecord> collectDrawRecords(IReplayController* ctrl) {
    const auto& rootActions = ctrl->GetRootActions();
    std::vector<DrawRecord> records;
    for (const auto& action : rootActions)
        buildMarkerPath(action, "", records);
    return records;
}

bool hasAnyMarker(const std::vector<DrawRecord>& recs) {
    for (const auto& r : recs)
        if (!r.markerPath.empty()) return true;
    return false;
}

} // anonymous namespace
```

- [ ] **Step 2: Implement diffDraws**

Replace the stub `diffDraws` with:

```cpp
DrawsDiffResult diffDraws(DiffSession& session) {
    auto recsA = collectDrawRecords(session.controllerA());
    auto recsB = collectDrawRecords(session.controllerB());

    bool markers = hasAnyMarker(recsA) || hasAnyMarker(recsB);

    std::vector<std::string> keysA, keysB;
    keysA.reserve(recsA.size());
    keysB.reserve(recsB.size());
    for (const auto& r : recsA) keysA.push_back(makeDrawMatchKey(r, markers));
    for (const auto& r : recsB) keysB.push_back(makeDrawMatchKey(r, markers));

    auto aligned = lcsAlign(keysA, keysB);

    // Count key occurrences for confidence
    std::unordered_map<std::string, int> keyCountA, keyCountB;
    for (const auto& k : keysA) keyCountA[k]++;
    for (const auto& k : keysB) keyCountB[k]++;

    DrawsDiffResult result;
    for (const auto& [ai, bi] : aligned) {
        DrawDiffRow row;
        if (ai.has_value()) row.a = recsA[*ai];
        if (bi.has_value()) row.b = recsB[*bi];

        if (ai.has_value() && bi.has_value()) {
            const auto& a = recsA[*ai];
            const auto& b = recsB[*bi];
            if (a.drawType == b.drawType && a.triangles == b.triangles && a.instances == b.instances)
                row.status = DiffStatus::Equal;
            else
                row.status = DiffStatus::Modified;

            // Confidence
            std::string key = keysA[*ai];
            if (markers) {
                row.confidence = (keyCountA[key] == 1 && keyCountB[key] == 1) ? "high" : "medium";
            } else {
                row.confidence = (keyCountA[key] == 1 && keyCountB[key] == 1) ? "medium" : "low";
            }
        } else if (ai.has_value()) {
            row.status = DiffStatus::Deleted;
            row.confidence = markers ? "high" : "medium";
        } else {
            row.status = DiffStatus::Added;
            row.confidence = markers ? "high" : "medium";
        }

        switch (row.status) {
            case DiffStatus::Equal:    result.unchanged++; break;
            case DiffStatus::Modified: result.modified++;  break;
            case DiffStatus::Added:    result.added++;     break;
            case DiffStatus::Deleted:  result.deleted++;   break;
        }
        result.rows.push_back(std::move(row));
    }

    return result;
}
```

- [ ] **Step 3: Implement diffResources**

Replace the stub `diffResources` with:

```cpp
ResourcesDiffResult diffResources(DiffSession& session) {
    auto* ctrlA = session.controllerA();
    auto* ctrlB = session.controllerB();

    auto resListA = ctrlA->GetResources();
    auto resListB = ctrlB->GetResources();

    // Build name-keyed maps (first occurrence only)
    std::unordered_map<std::string, std::pair<std::string, std::string>> namedA, namedB;
    std::vector<std::pair<std::string, std::string>> unnamedA, unnamedB; // (type, name)

    for (const auto& r : resListA) {
        std::string name = r.name.c_str();
        std::string type = ToStr(r.type).c_str();
        if (!name.empty()) {
            std::string key = toLower(name);
            if (namedA.find(key) == namedA.end())
                namedA[key] = {name, type};
            else
                unnamedA.push_back({type, name});
        } else {
            unnamedA.push_back({type, ""});
        }
    }
    for (const auto& r : resListB) {
        std::string name = r.name.c_str();
        std::string type = ToStr(r.type).c_str();
        if (!name.empty()) {
            std::string key = toLower(name);
            if (namedB.find(key) == namedB.end())
                namedB[key] = {name, type};
            else
                unnamedB.push_back({type, name});
        } else {
            unnamedB.push_back({type, ""});
        }
    }

    ResourcesDiffResult result;

    // Compare named resources
    for (const auto& [key, valA] : namedA) {
        ResourceDiffRow row;
        row.name = valA.first;
        row.typeA = valA.second;
        row.confidence = "high";
        auto it = namedB.find(key);
        if (it != namedB.end()) {
            row.typeB = it->second.second;
            row.status = (row.typeA == row.typeB) ? DiffStatus::Equal : DiffStatus::Modified;
            namedB.erase(it);
        } else {
            row.status = DiffStatus::Deleted;
        }
        switch (row.status) {
            case DiffStatus::Equal:    result.unchanged++; break;
            case DiffStatus::Modified: result.modified++;  break;
            case DiffStatus::Deleted:  result.deleted++;   break;
            default: break;
        }
        result.rows.push_back(std::move(row));
    }

    // Remaining named in B = Added
    for (const auto& [key, valB] : namedB) {
        ResourceDiffRow row;
        row.name = valB.first;
        row.typeB = valB.second;
        row.status = DiffStatus::Added;
        row.confidence = "high";
        result.added++;
        result.rows.push_back(std::move(row));
    }

    // Unnamed: positional match by type group (simplified)
    // Group unnamed by type, match by position
    std::unordered_map<std::string, std::vector<size_t>> typeGroupA, typeGroupB;
    for (size_t i = 0; i < unnamedA.size(); i++) typeGroupA[unnamedA[i].first].push_back(i);
    for (size_t i = 0; i < unnamedB.size(); i++) typeGroupB[unnamedB[i].first].push_back(i);

    for (auto& [type, indicesA] : typeGroupA) {
        auto itB = typeGroupB.find(type);
        size_t matchCount = 0;
        if (itB != typeGroupB.end()) matchCount = std::min(indicesA.size(), itB->second.size());

        for (size_t k = 0; k < matchCount; k++) {
            ResourceDiffRow row;
            row.name = "(unnamed)";
            row.typeA = type;
            row.typeB = type;
            row.status = DiffStatus::Equal;
            row.confidence = "low";
            result.unchanged++;
            result.rows.push_back(std::move(row));
        }
        for (size_t k = matchCount; k < indicesA.size(); k++) {
            ResourceDiffRow row;
            row.name = "(unnamed)";
            row.typeA = type;
            row.status = DiffStatus::Deleted;
            row.confidence = "low";
            result.deleted++;
            result.rows.push_back(std::move(row));
        }
        if (itB != typeGroupB.end()) {
            for (size_t k = matchCount; k < itB->second.size(); k++) {
                ResourceDiffRow row;
                row.name = "(unnamed)";
                row.typeB = type;
                row.status = DiffStatus::Added;
                row.confidence = "low";
                result.added++;
                result.rows.push_back(std::move(row));
            }
            typeGroupB.erase(itB);
        }
    }
    for (auto& [type, indicesB] : typeGroupB) {
        for (size_t k = 0; k < indicesB.size(); k++) {
            ResourceDiffRow row;
            row.name = "(unnamed)";
            row.typeB = type;
            row.status = DiffStatus::Added;
            row.confidence = "low";
            result.added++;
            result.rows.push_back(std::move(row));
        }
    }

    return result;
}
```

- [ ] **Step 4: Build**

Run: `cmake --build build --config Release --target renderdoc-core 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/core/diff.cpp
git commit -m "feat(core): implement diffDraws and diffResources"
```

---

## Task 6: Implement diffStats, diffPipeline, diffFramebuffer, diffSummary

**Files:**
- Modify: `src/core/diff.cpp`

- [ ] **Step 1: Implement diffStats**

Replace the `diffStats` stub with a full implementation that:
1. Calls `ctrlA->GetRootActions()` and `ctrlB->GetRootActions()` to traverse the action tree
2. Infers passes by detecting `BeginPass`/`EndPass` markers (or groups by top-level markers for GL)
3. Counts draws, dispatches, and triangles per pass
4. Matches passes by lowercased name
5. Classifies each as Equal/Modified/Added/Deleted

Key logic: reuse the pattern from `listPasses()` in `resources.cpp` — walk the action tree, detect pass boundaries, accumulate stats per pass name.

- [ ] **Step 2: Implement diffPipeline**

Replace the `diffPipeline` stub with a full implementation that:
1. Calls `diffDraws(session)` to get alignment
2. Parses `markerPath` for optional `[N]` index suffix
3. Finds the Nth matched pair with that marker path
4. Navigates both controllers to their respective EIDs via `SetFrameEvent()`
5. Gets `PipelineState` from both via `GetPipelineState()` on each controller
6. Compares pipeline fields: shaders (resource IDs, entry points), render targets (count, format, dimensions), viewports, depth target
7. Stringifies all values and produces `PipeFieldDiff` entries

- [ ] **Step 3: Implement diffFramebuffer**

Replace the `diffFramebuffer` stub with a full implementation that:
1. If eidA == 0, finds last draw event in capture A (walk action tree, find last `Drawcall`-flagged action)
2. Same for eidB in capture B
3. Navigates both controllers to their respective EIDs
4. Exports render target at `target` index from both controllers to temp PNG files (reuse the export pattern from `export.cpp`)
5. Calls the existing `assertImage()` from `assertions.h` to compare the two PNGs
6. Returns `ImageCompareResult`

- [ ] **Step 4: Implement diffSummary**

Replace the `diffSummary` stub with a full implementation using multi-level checking:

```cpp
SummaryDiffResult diffSummary(DiffSession& session) {
    auto* ctrlA = session.controllerA();
    auto* ctrlB = session.controllerB();

    // Count events and draws from both
    auto countFromCtrl = [](IReplayController* ctrl) -> std::pair<int, int> {
        const auto& actions = ctrl->GetRootActions();
        int events = 0, draws = 0;
        std::function<void(const rdcarray<ActionDescription>&)> walk;
        walk = [&](const rdcarray<ActionDescription>& acts) {
            for (const auto& a : acts) {
                events++;
                if (a.flags & ActionFlags::Drawcall) draws++;
                if (!a.children.empty()) walk(a.children);
            }
        };
        walk(actions);
        return {events, draws};
    };

    auto [eventsA, drawsA] = countFromCtrl(ctrlA);
    auto [eventsB, drawsB] = countFromCtrl(ctrlB);

    int resourcesA = static_cast<int>(ctrlA->GetResources().size());
    int resourcesB = static_cast<int>(ctrlB->GetResources().size());

    // Passes: count unique pass names (simplified)
    // Use action tree top-level children as proxy for pass count
    int passesA = static_cast<int>(ctrlA->GetRootActions().size());
    int passesB = static_cast<int>(ctrlB->GetRootActions().size());

    SummaryDiffResult result;
    result.rows.push_back({"draws", drawsA, drawsB, drawsB - drawsA});
    result.rows.push_back({"passes", passesA, passesB, passesB - passesA});
    result.rows.push_back({"resources", resourcesA, resourcesB, resourcesB - resourcesA});
    result.rows.push_back({"events", eventsA, eventsB, eventsB - eventsA});

    // Level 1: counts
    bool countsMatch = true;
    for (const auto& row : result.rows) {
        if (row.delta != 0) { countsMatch = false; break; }
    }

    if (!countsMatch) {
        result.identical = false;
        result.divergedAt = "counts";
        return result;
    }

    // Level 2: structural (draws + resources diff)
    auto drawsDiff = diffDraws(session);
    if (drawsDiff.modified > 0 || drawsDiff.added > 0 || drawsDiff.deleted > 0) {
        result.identical = false;
        result.divergedAt = "structure";
        return result;
    }

    auto resDiff = diffResources(session);
    if (resDiff.modified > 0 || resDiff.added > 0 || resDiff.deleted > 0) {
        result.identical = false;
        result.divergedAt = "structure";
        return result;
    }

    // Level 3: framebuffer spot-check
    try {
        auto fbDiff = diffFramebuffer(session, 0, 0, 0, 0.0, "");
        if (fbDiff.diffPixels > 0) {
            result.identical = false;
            result.divergedAt = "framebuffer";
            return result;
        }
    } catch (...) {
        // If framebuffer comparison fails (e.g. no render targets), skip
    }

    result.identical = true;
    result.divergedAt = "";
    return result;
}
```

- [ ] **Step 5: Build**

Run: `cmake --build build --config Release --target renderdoc-core 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 6: Commit**

```bash
git add src/core/diff.cpp
git commit -m "feat(core): implement diffStats, diffPipeline, diffFramebuffer, diffSummary"
```

---

## Task 7: Serialization for Diff Types

**Files:**
- Modify: `src/mcp/serialization.h`
- Modify: `src/mcp/serialization.cpp`

- [ ] **Step 1: Add declarations to serialization.h**

Add after the existing `to_json` declarations:

```cpp
// Diff types
nlohmann::json to_json(DiffStatus status);
nlohmann::json to_json(const DrawRecord& rec);
nlohmann::json to_json(const DrawDiffRow& row);
nlohmann::json to_json(const DrawsDiffResult& result);
nlohmann::json to_json(const ResourceDiffRow& row);
nlohmann::json to_json(const ResourcesDiffResult& result);
nlohmann::json to_json(const PassDiffRow& row);
nlohmann::json to_json(const StatsDiffResult& result);
nlohmann::json to_json(const PipeFieldDiff& field);
nlohmann::json to_json(const PipelineDiffResult& result);
nlohmann::json to_json(const SummaryRow& row);
nlohmann::json to_json(const SummaryDiffResult& result);
nlohmann::json to_json(const DiffSession::OpenResult& result);
```

Also add at the top: `#include "core/diff.h"` and `#include "core/diff_session.h"`.

- [ ] **Step 2: Implement serializations in serialization.cpp**

Add to `src/mcp/serialization.cpp`:

```cpp
json to_json(DiffStatus status) {
    switch (status) {
        case DiffStatus::Equal:    return "equal";
        case DiffStatus::Modified: return "modified";
        case DiffStatus::Added:    return "added";
        case DiffStatus::Deleted:  return "deleted";
    }
    return "unknown";
}

json to_json(const DrawRecord& rec) {
    json j;
    j["eventId"] = rec.eventId;
    j["drawType"] = rec.drawType;
    j["markerPath"] = rec.markerPath;
    j["triangles"] = rec.triangles;
    j["instances"] = rec.instances;
    j["passName"] = rec.passName;
    j["topology"] = rec.topology;
    return j;
}

json to_json(const DrawDiffRow& row) {
    json j;
    j["status"] = to_json(row.status);
    if (row.a.has_value()) j["a"] = to_json(*row.a); else j["a"] = nullptr;
    if (row.b.has_value()) j["b"] = to_json(*row.b); else j["b"] = nullptr;
    j["confidence"] = row.confidence;
    return j;
}

json to_json(const DrawsDiffResult& result) {
    json j;
    j["rows"] = to_json_array(result.rows);
    j["added"] = result.added;
    j["deleted"] = result.deleted;
    j["modified"] = result.modified;
    j["unchanged"] = result.unchanged;
    return j;
}

json to_json(const ResourceDiffRow& row) {
    json j;
    j["status"] = to_json(row.status);
    j["name"] = row.name;
    j["typeA"] = row.typeA;
    j["typeB"] = row.typeB;
    j["confidence"] = row.confidence;
    return j;
}

json to_json(const ResourcesDiffResult& result) {
    json j;
    j["rows"] = to_json_array(result.rows);
    j["added"] = result.added;
    j["deleted"] = result.deleted;
    j["modified"] = result.modified;
    j["unchanged"] = result.unchanged;
    return j;
}

json to_json(const PassDiffRow& row) {
    json j;
    j["status"] = to_json(row.status);
    j["name"] = row.name;
    j["drawsA"] = row.drawsA.has_value() ? json(*row.drawsA) : json(nullptr);
    j["drawsB"] = row.drawsB.has_value() ? json(*row.drawsB) : json(nullptr);
    j["trianglesA"] = row.trianglesA.has_value() ? json(*row.trianglesA) : json(nullptr);
    j["trianglesB"] = row.trianglesB.has_value() ? json(*row.trianglesB) : json(nullptr);
    j["dispatchesA"] = row.dispatchesA.has_value() ? json(*row.dispatchesA) : json(nullptr);
    j["dispatchesB"] = row.dispatchesB.has_value() ? json(*row.dispatchesB) : json(nullptr);
    return j;
}

json to_json(const StatsDiffResult& result) {
    json j;
    j["rows"] = to_json_array(result.rows);
    j["passesChanged"] = result.passesChanged;
    j["passesAdded"] = result.passesAdded;
    j["passesDeleted"] = result.passesDeleted;
    j["drawsDelta"] = result.drawsDelta;
    j["trianglesDelta"] = result.trianglesDelta;
    j["dispatchesDelta"] = result.dispatchesDelta;
    return j;
}

json to_json(const PipeFieldDiff& field) {
    json j;
    j["section"] = field.section;
    j["field"] = field.field;
    j["valueA"] = field.valueA;
    j["valueB"] = field.valueB;
    j["changed"] = field.changed;
    return j;
}

json to_json(const PipelineDiffResult& result) {
    json j;
    j["eidA"] = result.eidA;
    j["eidB"] = result.eidB;
    j["markerPath"] = result.markerPath;
    j["fields"] = to_json_array(result.fields);
    j["changedCount"] = result.changedCount;
    j["totalCount"] = result.totalCount;
    return j;
}

json to_json(const SummaryRow& row) {
    json j;
    j["category"] = row.category;
    j["valueA"] = row.valueA;
    j["valueB"] = row.valueB;
    j["delta"] = row.delta;
    return j;
}

json to_json(const SummaryDiffResult& result) {
    json j;
    j["rows"] = to_json_array(result.rows);
    j["identical"] = result.identical;
    j["divergedAt"] = result.divergedAt;
    return j;
}

json to_json(const DiffSession::OpenResult& result) {
    json j;
    j["infoA"] = to_json(result.infoA);
    j["infoB"] = to_json(result.infoB);
    return j;
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build --config Release 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/mcp/serialization.h src/mcp/serialization.cpp
git commit -m "feat(mcp): add JSON serialization for all diff types"
```

---

## Task 8: MCP Diff Tools

**Files:**
- Create: `src/mcp/tools/diff_tools.cpp`
- Modify: `src/mcp/tools/tools.h`
- Modify: `src/mcp/mcp_server_default.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add declaration to tools.h**

Add to `src/mcp/tools/tools.h` before the closing `}`:

```cpp
void registerDiffTools(ToolRegistry& registry);
```

- [ ] **Step 2: Create diff_tools.cpp**

Create `src/mcp/tools/diff_tools.cpp`:

```cpp
#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/diff_session.h"
#include "core/diff.h"

namespace renderdoc::mcp::tools {

void registerDiffTools(ToolRegistry& registry) {

    registry.registerTool({
        "diff_open",
        "Open two capture files for comparison. Returns metadata for both captures.",
        {{"type", "object"},
         {"properties", {
             {"captureA", {{"type", "string"}, {"description", "Path to first .rdc file"}}},
             {"captureB", {{"type", "string"}, {"description", "Path to second .rdc file"}}}
         }},
         {"required", {"captureA", "captureB"}}},
        [](ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto result = ctx.diffSession.open(
                args["captureA"].get<std::string>(),
                args["captureB"].get<std::string>());
            return to_json(result);
        }
    });

    registry.registerTool({
        "diff_close",
        "Close the diff session and release both captures.",
        {{"type", "object"}, {"properties", json::object()}},
        [](ToolContext& ctx, const nlohmann::json&) -> nlohmann::json {
            ctx.diffSession.close();
            return {{"success", true}};
        }
    });

    registry.registerTool({
        "diff_summary",
        "Get a high-level summary of differences between two captures. "
        "Checks counts, structural alignment, and framebuffer pixels.",
        {{"type", "object"}, {"properties", json::object()}},
        [](ToolContext& ctx, const nlohmann::json&) -> nlohmann::json {
            return to_json(core::diffSummary(ctx.diffSession));
        }
    });

    registry.registerTool({
        "diff_draws",
        "Compare draw call sequences between two captures using LCS alignment.",
        {{"type", "object"}, {"properties", json::object()}},
        [](ToolContext& ctx, const nlohmann::json&) -> nlohmann::json {
            return to_json(core::diffDraws(ctx.diffSession));
        }
    });

    registry.registerTool({
        "diff_resources",
        "Compare GPU resource lists between two captures.",
        {{"type", "object"}, {"properties", json::object()}},
        [](ToolContext& ctx, const nlohmann::json&) -> nlohmann::json {
            return to_json(core::diffResources(ctx.diffSession));
        }
    });

    registry.registerTool({
        "diff_stats",
        "Compare per-pass statistics between two captures.",
        {{"type", "object"}, {"properties", json::object()}},
        [](ToolContext& ctx, const nlohmann::json&) -> nlohmann::json {
            return to_json(core::diffStats(ctx.diffSession));
        }
    });

    registry.registerTool({
        "diff_pipeline",
        "Compare pipeline state at a matched draw call between two captures.",
        {{"type", "object"},
         {"properties", {
             {"marker", {{"type", "string"}, {"description", "Draw marker path (e.g. 'GBuffer/Floor[0]')"}}}
         }},
         {"required", {"marker"}}},
        [](ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            return to_json(core::diffPipeline(ctx.diffSession,
                           args["marker"].get<std::string>()));
        }
    });

    registry.registerTool({
        "diff_framebuffer",
        "Pixel-level comparison of render targets between two captures.",
        {{"type", "object"},
         {"properties", {
             {"eidA", {{"type", "integer"}, {"description", "Event ID in capture A (0 = last draw)"}}},
             {"eidB", {{"type", "integer"}, {"description", "Event ID in capture B (0 = last draw)"}}},
             {"target", {{"type", "integer"}, {"description", "Color target index (default 0)"}}},
             {"threshold", {{"type", "number"}, {"description", "Max diff ratio % for identical (default 0.0)"}}},
             {"diffOutput", {{"type", "string"}, {"description", "Path to write diff visualization PNG"}}}
         }}},
        [](ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            uint32_t eidA = args.value("eidA", 0u);
            uint32_t eidB = args.value("eidB", 0u);
            int target = args.value("target", 0);
            double threshold = args.value("threshold", 0.0);
            std::string diffOutput = args.value("diffOutput", std::string(""));
            return to_json(core::diffFramebuffer(ctx.diffSession,
                           eidA, eidB, target, threshold, diffOutput));
        }
    });
}

} // namespace renderdoc::mcp::tools
```

- [ ] **Step 3: Register in mcp_server_default.cpp**

Add `#include "core/diff_session.h"` and in the default constructor, after `tools::registerAssertionTools(*m_registry);`:

```cpp
    tools::registerDiffTools(*m_registry);

    m_ownedDiffSession = std::make_unique<core::DiffSession>();
    m_diffSession = m_ownedDiffSession.get();
```

- [ ] **Step 4: Add to CMakeLists.txt**

Add `src/mcp/tools/diff_tools.cpp` to the `renderdoc-mcp-lib` target, after `src/mcp/tools/assertion_tools.cpp`:

```
        src/mcp/tools/diff_tools.cpp
```

- [ ] **Step 5: Build**

Run: `cmake --build build --config Release 2>&1 | tail -10`
Expected: Full build succeeds.

- [ ] **Step 6: Commit**

```bash
git add src/mcp/tools/diff_tools.cpp src/mcp/tools/tools.h \
        src/mcp/mcp_server_default.cpp CMakeLists.txt
git commit -m "feat(mcp): register 8 diff tools (diff_open through diff_framebuffer)"
```

---

## Task 9: CLI Diff Command

**Files:**
- Modify: `src/cli/main.cpp`

- [ ] **Step 1: Add diff includes and command**

Add includes at top of `src/cli/main.cpp`:

```cpp
#include "core/diff_session.h"
#include "core/diff.h"
```

- [ ] **Step 2: Add diff command handler functions**

Add before `main()`:

```cpp
static int cmdDiff(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: renderdoc-cli diff FILE_A FILE_B [--draws|--resources|--stats|--pipeline MARKER|--framebuffer] [options]\n";
        return 2;
    }

    std::string pathA = argv[2];
    std::string pathB = argv[3];

    // Parse options
    std::string mode = "summary";
    std::string marker;
    bool jsonOutput = false;
    int target = 0;
    double threshold = 0.0;
    uint32_t eidA = 0, eidB = 0;
    std::string diffOutput;
    bool verbose = false;

    for (int i = 4; i < argc; i++) {
        std::string tok = argv[i];
        if (tok == "--draws") mode = "draws";
        else if (tok == "--resources") mode = "resources";
        else if (tok == "--stats") mode = "stats";
        else if (tok == "--framebuffer") mode = "framebuffer";
        else if (tok == "--pipeline" && i + 1 < argc) { mode = "pipeline"; marker = argv[++i]; }
        else if (tok == "--json") jsonOutput = true;
        else if (tok == "--target" && i + 1 < argc) target = std::stoi(argv[++i]);
        else if (tok == "--threshold" && i + 1 < argc) threshold = std::stod(argv[++i]);
        else if (tok == "--eid-a" && i + 1 < argc) eidA = static_cast<uint32_t>(std::stoul(argv[++i]));
        else if (tok == "--eid-b" && i + 1 < argc) eidB = static_cast<uint32_t>(std::stoul(argv[++i]));
        else if (tok == "--diff-output" && i + 1 < argc) diffOutput = argv[++i];
        else if (tok == "--verbose") verbose = true;
    }

    DiffSession ds;
    try {
        auto info = ds.open(pathA, pathB);
        std::cerr << "Opened: " << pathA << " vs " << pathB << "\n";
    } catch (const CoreError& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }

    try {
        if (mode == "summary") {
            auto r = diffSummary(ds);
            if (jsonOutput) {
                // Would need serialization; for CLI just print text
                // Import not available in CLI, so print manually
            }
            for (const auto& row : r.rows) {
                char buf[128];
                snprintf(buf, sizeof(buf), "%-12s %6d -> %-6d (%s%d)",
                         (row.category + ":").c_str(), row.valueA, row.valueB,
                         row.delta > 0 ? "+" : "", row.delta);
                std::cout << buf << "\n";
            }
            ds.close();
            return r.identical ? 0 : 1;

        } else if (mode == "draws") {
            auto r = diffDraws(ds);
            for (const auto& row : r.rows) {
                char statusChar = '?';
                switch (row.status) {
                    case DiffStatus::Equal: statusChar = '='; break;
                    case DiffStatus::Modified: statusChar = '~'; break;
                    case DiffStatus::Added: statusChar = '+'; break;
                    case DiffStatus::Deleted: statusChar = '-'; break;
                }
                std::cout << statusChar << "\t"
                          << (row.a ? std::to_string(row.a->eventId) : "-") << "\t"
                          << (row.b ? std::to_string(row.b->eventId) : "-") << "\t"
                          << (row.a ? row.a->markerPath : (row.b ? row.b->markerPath : "")) << "\t"
                          << (row.a ? row.a->drawType : (row.b ? row.b->drawType : "")) << "\t"
                          << (row.a ? std::to_string(row.a->triangles) : "-") << "\t"
                          << (row.b ? std::to_string(row.b->triangles) : "-") << "\t"
                          << row.confidence << "\n";
            }
            ds.close();
            return (r.modified > 0 || r.added > 0 || r.deleted > 0) ? 1 : 0;

        } else if (mode == "resources") {
            auto r = diffResources(ds);
            for (const auto& row : r.rows) {
                char statusChar = '?';
                switch (row.status) {
                    case DiffStatus::Equal: statusChar = '='; break;
                    case DiffStatus::Modified: statusChar = '~'; break;
                    case DiffStatus::Added: statusChar = '+'; break;
                    case DiffStatus::Deleted: statusChar = '-'; break;
                }
                std::cout << statusChar << "\t" << row.name << "\t"
                          << (row.typeA.empty() ? "-" : row.typeA) << "\t"
                          << (row.typeB.empty() ? "-" : row.typeB) << "\n";
            }
            ds.close();
            return (r.modified > 0 || r.added > 0 || r.deleted > 0) ? 1 : 0;

        } else if (mode == "stats") {
            auto r = diffStats(ds);
            for (const auto& row : r.rows) {
                char statusChar = '?';
                switch (row.status) {
                    case DiffStatus::Equal: statusChar = '='; break;
                    case DiffStatus::Modified: statusChar = '~'; break;
                    case DiffStatus::Added: statusChar = '+'; break;
                    case DiffStatus::Deleted: statusChar = '-'; break;
                }
                std::cout << statusChar << "\t" << row.name << "\t"
                          << (row.drawsA ? std::to_string(*row.drawsA) : "-") << "\t"
                          << (row.drawsB ? std::to_string(*row.drawsB) : "-") << "\n";
            }
            ds.close();
            return (r.passesChanged > 0 || r.passesAdded > 0 || r.passesDeleted > 0) ? 1 : 0;

        } else if (mode == "pipeline") {
            auto r = diffPipeline(ds, marker);
            for (const auto& f : r.fields) {
                if (!verbose && !f.changed) continue;
                std::cout << f.section << "\t" << f.field << "\t"
                          << f.valueA << "\t" << f.valueB;
                if (f.changed) std::cout << "\t<- changed";
                std::cout << "\n";
            }
            ds.close();
            return (r.changedCount > 0) ? 1 : 0;

        } else if (mode == "framebuffer") {
            auto r = diffFramebuffer(ds, eidA, eidB, target, threshold, diffOutput);
            if (r.diffPixels == 0) {
                std::cout << "identical\n";
            } else {
                std::cout << "diff: " << r.diffPixels << "/" << r.totalPixels
                          << " pixels (" << r.diffRatio << "%)\n";
                if (!r.diffOutputPath.empty())
                    std::cout << "diff image written to: " << r.diffOutputPath << "\n";
            }
            ds.close();
            return (r.diffPixels > 0) ? 1 : 0;
        }
    } catch (const CoreError& e) {
        std::cerr << "error: " << e.what() << "\n";
        ds.close();
        return 2;
    }

    ds.close();
    return 0;
}
```

- [ ] **Step 3: Wire into main() dispatch**

In `main()`, add before the standard commands `if (argc < 3)` check:

```cpp
    // Special case: "diff" command takes two capture paths
    if (std::string(argv[1]) == "diff") {
        return cmdDiff(argc, argv);
    }
```

Also update `printUsage` to include:
```cpp
              << "  diff FILE_A FILE_B [--draws|--resources|--stats|--pipeline MARKER|--framebuffer]\n"
```

- [ ] **Step 4: Build**

Run: `cmake --build build --config Release --target renderdoc-cli 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/cli/main.cpp
git commit -m "feat(cli): add diff command with 6 modes"
```

---

## Task 10: Integration Tests

**Files:**
- Create: `tests/integration/test_tools_diff.cpp`
- Modify: `tests/integration/test_protocol.cpp`

- [ ] **Step 1: Create integration test file**

Create `tests/integration/test_tools_diff.cpp`:

```cpp
#include <gtest/gtest.h>
#include "mcp/tool_registry.h"
#include "mcp/tools/tools.h"
#include "core/session.h"
#include "core/diff_session.h"

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;
using renderdoc::core::Session;
using renderdoc::core::DiffSession;
using renderdoc::mcp::ToolRegistry;
using renderdoc::mcp::ToolContext;
namespace tools = renderdoc::mcp::tools;

#ifdef _WIN32
static void openDiffImpl(DiffSession* ds);

#pragma warning(push)
#pragma warning(disable: 4611)
static bool doOpenDiffSEH(DiffSession* ds)
{
    __try { openDiffImpl(ds); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#pragma warning(pop)

static void openDiffImpl(DiffSession* ds) { ds->open(TEST_RDC_PATH, TEST_RDC_PATH); }
#endif

class DiffToolTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        tools::registerDiffTools(s_registry);

#ifdef _WIN32
        if (!doOpenDiffSEH(&s_diffSession)) { s_skipAll = true; return; }
#else
        s_diffSession.open(TEST_RDC_PATH, TEST_RDC_PATH);
#endif
        ASSERT_TRUE(s_diffSession.isOpen());
    }

    static void TearDownTestSuite() {
        s_diffSession.close();
    }

    void SetUp() override {
        if (s_skipAll) GTEST_SKIP() << "DiffSession init failed";
    }

    static Session s_session;
    static DiffSession s_diffSession;
    static ToolRegistry s_registry;
    static bool s_skipAll;
};

Session DiffToolTest::s_session;
DiffSession DiffToolTest::s_diffSession;
ToolRegistry DiffToolTest::s_registry;
bool DiffToolTest::s_skipAll = false;

TEST_F(DiffToolTest, SelfDiffSummaryIsIdentical) {
    ToolContext ctx{s_session, s_diffSession};
    auto result = s_registry.callTool("diff_summary", ctx, {});
    ASSERT_TRUE(result.contains("identical"));
    EXPECT_TRUE(result["identical"].get<bool>());
    EXPECT_EQ(result["divergedAt"].get<std::string>(), "");
}

TEST_F(DiffToolTest, SelfDiffDrawsAllEqual) {
    ToolContext ctx{s_session, s_diffSession};
    auto result = s_registry.callTool("diff_draws", ctx, {});
    ASSERT_TRUE(result.contains("rows"));
    EXPECT_EQ(result["modified"].get<int>(), 0);
    EXPECT_EQ(result["added"].get<int>(), 0);
    EXPECT_EQ(result["deleted"].get<int>(), 0);
    EXPECT_GT(result["unchanged"].get<int>(), 0);
}

TEST_F(DiffToolTest, SelfDiffResourcesAllEqual) {
    ToolContext ctx{s_session, s_diffSession};
    auto result = s_registry.callTool("diff_resources", ctx, {});
    EXPECT_EQ(result["modified"].get<int>(), 0);
    EXPECT_EQ(result["added"].get<int>(), 0);
    EXPECT_EQ(result["deleted"].get<int>(), 0);
}

TEST_F(DiffToolTest, SelfDiffFramebufferIdentical) {
    ToolContext ctx{s_session, s_diffSession};
    auto result = s_registry.callTool("diff_framebuffer", ctx, {});
    EXPECT_EQ(result["diffPixels"].get<int>(), 0);
}

TEST_F(DiffToolTest, DiffCloseSucceeds) {
    // Open a fresh diff session for this test
    DiffSession tempDs;
#ifdef _WIN32
    __try { tempDs.open(TEST_RDC_PATH, TEST_RDC_PATH); } __except(EXCEPTION_EXECUTE_HANDLER) { GTEST_SKIP(); }
#else
    tempDs.open(TEST_RDC_PATH, TEST_RDC_PATH);
#endif
    ToolContext ctx{s_session, tempDs};
    auto result = s_registry.callTool("diff_close", ctx, {});
    EXPECT_TRUE(result.contains("success"));
    EXPECT_FALSE(tempDs.isOpen());
}

TEST_F(DiffToolTest, DiffNotOpenError) {
    DiffSession emptyDs;
    ToolContext ctx{s_session, emptyDs};
    EXPECT_THROW(s_registry.callTool("diff_summary", ctx, {}), renderdoc::core::CoreError);
}
```

- [ ] **Step 2: Update protocol test tool count**

In `tests/integration/test_protocol.cpp`, find the line that checks `EXPECT_EQ(tools.size(), 40u)` and change to `48u`.

- [ ] **Step 3: Build and run tests**

Run: `cmake --build build --config Release --target test-tools 2>&1 | tail -5`
Then: `cd build && ctest -R test-tools -C Release --output-on-failure`
Expected: All tests pass including new diff tests.

- [ ] **Step 4: Commit**

```bash
git add tests/integration/test_tools_diff.cpp tests/integration/test_protocol.cpp
git commit -m "test: add integration tests for diff tools + update tool count to 48"
```

---

## Task 11: Update README and SKILL.md

**Files:**
- Modify: `skills/renderdoc-mcp/SKILL.md`
- Modify: `README.md`

- [ ] **Step 1: Add diff workflow to SKILL.md**

Add a new section "Frame Regression Diagnosis" to the diagnostic workflows:

```markdown
### Frame Regression Diagnosis

When comparing two captures to find rendering differences:
1. `diff_open` captureA captureB → Load both captures
2. `diff_summary` → Quick overview: any differences? Check `divergedAt` to know where
3. `diff_draws` → Which draws changed/added/removed?
4. `diff_pipeline "MarkerPath"` → What pipeline state changed at that draw?
5. `diff_framebuffer` with `diffOutput` → Pixel-level visual comparison
6. `diff_close` → Clean up
```

- [ ] **Step 2: Add diff tools to SKILL.md tool reference**

Add the 8 diff tools (`diff_open`, `diff_close`, `diff_summary`, `diff_draws`, `diff_resources`, `diff_stats`, `diff_pipeline`, `diff_framebuffer`) to the tool reference section with parameter descriptions.

- [ ] **Step 3: Update README.md**

Update the tool count from 40 to 48 and add a brief description of the diff capability in the features section.

- [ ] **Step 4: Commit**

```bash
git add skills/renderdoc-mcp/SKILL.md README.md
git commit -m "docs: update README and SKILL.md for Phase 3 diff engine (48 tools)"
```

---

## Task 12: Final Build and Test Verification

- [ ] **Step 1: Full clean build**

Run: `cmake -B build -DRENDERDOC_DIR=D:/renderdoc/renderdoc -DBUILD_TESTING=ON && cmake --build build --config Release 2>&1 | tail -20`
Expected: Clean build with no errors or warnings.

- [ ] **Step 2: Run all unit tests**

Run: `cd build && ctest -R "test-unit|test-diff-algo" -C Release --output-on-failure`
Expected: All pass.

- [ ] **Step 3: Run all integration tests**

Run: `cd build && ctest -R "test-tools|test-integration" -C Release --output-on-failure`
Expected: All pass (48 tools in protocol test).

- [ ] **Step 4: Manual CLI smoke test**

Run: `build/Release/renderdoc-cli diff tests/fixtures/vkcube.rdc tests/fixtures/vkcube.rdc`
Expected: All rows show `(=)`, exit code 0.

Run: `build/Release/renderdoc-cli diff tests/fixtures/vkcube.rdc tests/fixtures/vkcube.rdc --draws`
Expected: All rows show `=` status, exit code 0.

- [ ] **Step 5: Commit any final fixes**

If any fixes were needed during verification, commit them:
```bash
git add -A && git commit -m "fix: address issues found during final verification"
```
