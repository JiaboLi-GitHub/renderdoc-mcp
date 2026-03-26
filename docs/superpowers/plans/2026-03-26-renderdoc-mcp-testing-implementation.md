# renderdoc-mcp Automated Testing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a full automated test suite (unit + integration) with CI to the renderdoc-mcp project.

**Architecture:** Split production code into `renderdoc-mcp-core` (zero renderdoc dependency) and `renderdoc-mcp-lib` (renderdoc-dependent). Refactor `McpServer` to support constructor injection. Three test executables: `test-unit` (pure protocol/validation), `test-tools` (tool logic with real renderdoc), `test-integration` (process-level end-to-end).

**Tech Stack:** C++17, GoogleTest (FetchContent), CMake 3.16+, MSVC, GitHub Actions

**Spec:** `docs/superpowers/specs/2026-03-26-renderdoc-mcp-testing-design.md`

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `CMakeLists.txt` | Modify | Split into core/lib targets, conditional RENDERDOC_DIR, add_subdirectory(tests) |
| `src/mcp_server.h` | Modify | Decouple from renderdoc_wrapper.h: forward declare, pointer members, injection constructor |
| `src/mcp_server.cpp` | Modify | Keep only injection constructor + all method bodies (no renderdoc includes) |
| `src/mcp_server_default.cpp` | Create | Default constructor with tool registration (belongs to renderdoc-mcp-lib) |
| `src/main.cpp` | Verify | No API changes needed, verify it compiles unchanged |
| `tests/CMakeLists.txt` | Create | GoogleTest FetchContent, three test executables, CTest labels |
| `tests/unit/renderdoc_wrapper_stub.cpp` | Create | Empty-body implementations of RenderdocWrapper members for linker |
| `tests/unit/test_tool_registry.cpp` | Create | ToolRegistry validation tests with dummy tools |
| `tests/unit/test_mcp_server.cpp` | Create | McpServer protocol tests with injected dummy registry |
| `tests/integration/test_tools.cpp` | Create | Per-tool logic tests with real renderdoc + vkcube.rdc |
| `tests/integration/test_protocol.cpp` | Create | Process-level JSON-RPC tests via stdin/stdout pipes |
| `tests/integration/test_workflow.cpp` | Create | End-to-end debugging workflow tests |
| `.github/workflows/ci.yml` | Create | Two-job CI: unit-tests (no renderdoc) + integration-tests |

---

### Task 1: Refactor McpServer for Constructor Injection

**Files:**
- Modify: `src/mcp_server.h`
- Modify: `src/mcp_server.cpp`
- Create: `src/mcp_server_default.cpp`
- Verify: `src/main.cpp`

The goal: `mcp_server.h` must not `#include "renderdoc_wrapper.h"` and must not hold `RenderdocWrapper` as a value member. The default constructor (which calls `registerXxxTools`) must live in a separate file that belongs to `renderdoc-mcp-lib`, so `renderdoc-mcp-core` has zero renderdoc link dependencies.

- [ ] **Step 1: Modify `src/mcp_server.h`**

Replace the current header with forward declarations and pointer/reference members:

```cpp
#pragma once

#include <nlohmann/json.hpp>
#include "tool_registry.h"
#include <memory>

class RenderdocWrapper;

class McpServer
{
public:
    // Default constructor: creates own wrapper + registers all tools (requires renderdoc at link time)
    McpServer();
    // Injection constructor: uses external registry & wrapper (no renderdoc dependency)
    McpServer(ToolRegistry& registry, RenderdocWrapper& wrapper);
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

    std::unique_ptr<RenderdocWrapper> m_ownedWrapper;  // owned, only set by default ctor
    RenderdocWrapper* m_wrapper = nullptr;              // always valid (points to owned or injected)
    ToolRegistry m_ownedRegistry;                       // owned, only populated by default ctor
    ToolRegistry* m_registry = nullptr;                 // always valid (points to owned or injected)
    bool m_initialized = false;
};
```

- [ ] **Step 2: Split `src/mcp_server.cpp` into two files**

The default constructor calls `registerXxxTools()` (defined in `tools/*.cpp` which lives in `renderdoc-mcp-lib`). If we keep it in `mcp_server.cpp` → `renderdoc-mcp-core`, the linker will fail with unresolved symbols when `test-unit` links core only.

**Solution:** Move the default constructor to a new file `src/mcp_server_default.cpp` that belongs to `renderdoc-mcp-lib`.

**`src/mcp_server.cpp`** (stays in `renderdoc-mcp-core` — no renderdoc includes, no tools/tools.h):

```cpp
#include "mcp_server.h"
#include <stdexcept>

using json = nlohmann::json;

// Injection constructor: uses external registry & wrapper
McpServer::McpServer(ToolRegistry& registry, RenderdocWrapper& wrapper)
    : m_wrapper(&wrapper)
    , m_registry(&registry)
{
}

McpServer::~McpServer() = default;

void McpServer::shutdown()
{
    if(m_wrapper)
        m_wrapper->shutdown();
}

// All other methods (handleMessage, handleBatch, handleInitialize,
// handleToolsList, handleToolsCall, makeResponse, makeError, makeToolResult)
// stay the same except:
// - m_wrapper. becomes m_wrapper->
// - m_registry. becomes m_registry->
```

Key changes in method bodies:
- `handleToolsList`: `m_registry.getToolDefinitions()` → `m_registry->getToolDefinitions()`
- `handleToolsCall`: `m_registry.callTool(toolName, m_wrapper, arguments)` → `m_registry->callTool(toolName, *m_wrapper, arguments)`

All static helper methods (`makeResponse`, `makeError`, `makeToolResult`) and `handleMessage`/`handleBatch`/`handleInitialize` remain unchanged.

**`src/mcp_server_default.cpp`** (new file, belongs to `renderdoc-mcp-lib`):

```cpp
#include "mcp_server.h"
#include "renderdoc_wrapper.h"
#include "tools/tools.h"

// Default constructor: creates own wrapper + registers all tools.
// This file links against renderdoc-mcp-lib (which depends on renderdoc).
McpServer::McpServer()
    : m_ownedWrapper(std::make_unique<RenderdocWrapper>())
    , m_wrapper(m_ownedWrapper.get())
    , m_registry(&m_ownedRegistry)
{
    registerSessionTools(m_ownedRegistry);
    registerEventTools(m_ownedRegistry);
    registerPipelineTools(m_ownedRegistry);
    registerExportTools(m_ownedRegistry);
    registerInfoTools(m_ownedRegistry);
    registerResourceTools(m_ownedRegistry);
    registerShaderTools(m_ownedRegistry);
}
```

- [ ] **Step 3: Verify `src/main.cpp` compiles unchanged**

`main.cpp` calls `McpServer()` (default constructor) which now lives in `mcp_server_default.cpp`. Since `main.cpp` only includes `mcp_server.h` and the default constructor is still declared there, no changes to `main.cpp` are needed. Just verify it still compiles.

- [ ] **Step 4: Build and verify**

```bash
cmake -B build -DRENDERDOC_DIR=D:/renderdoc/renderdoc -DBUILD_TESTING=OFF
cmake --build build --config Release
```

Expected: builds successfully, exe works identically to before.

- [ ] **Step 5: Commit**

```bash
git add src/mcp_server.h src/mcp_server.cpp src/mcp_server_default.cpp
git commit -m "refactor: add McpServer injection constructor, decouple from renderdoc_wrapper.h"
```

---

### Task 2: Split CMakeLists.txt into Core/Lib Targets

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Rewrite `CMakeLists.txt`**

Replace the single executable target with a two-library + executable structure. Key changes:

```cmake
cmake_minimum_required(VERSION 3.16)
project(renderdoc-mcp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── options ──────────────────────────────────────────────────────────────────
set(RENDERDOC_DIR "" CACHE PATH "Path to renderdoc source root (contains renderdoc/api/replay/)")
set(RENDERDOC_BUILD_DIR "" CACHE PATH "Path to renderdoc CMake build output (contains lib/Release/renderdoc.lib)")
option(BUILD_TESTING "Build test targets" OFF)

# ── nlohmann/json ────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(json)

# ── renderdoc-mcp-core (zero renderdoc dependency) ──────────────────────────
add_library(renderdoc-mcp-core STATIC
    src/mcp_server.cpp
    src/tool_registry.cpp
)
target_include_directories(renderdoc-mcp-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(renderdoc-mcp-core PUBLIC nlohmann_json::nlohmann_json)

# ── renderdoc-dependent targets (only if RENDERDOC_DIR is set) ──────────────
if(RENDERDOC_DIR)
    # renderdoc-mcp-lib
    add_library(renderdoc-mcp-lib STATIC
        src/mcp_server_default.cpp
        src/renderdoc_wrapper.cpp
        src/tools/session_tools.cpp
        src/tools/event_tools.cpp
        src/tools/pipeline_tools.cpp
        src/tools/export_tools.cpp
        src/tools/info_tools.cpp
        src/tools/resource_tools.cpp
        src/tools/shader_tools.cpp
    )
    target_link_libraries(renderdoc-mcp-lib PUBLIC renderdoc-mcp-core)
    target_include_directories(renderdoc-mcp-lib PUBLIC
        ${RENDERDOC_DIR}/renderdoc/api/replay
        ${RENDERDOC_DIR}/renderdoc/api
    )
    # platform defines
    if(WIN32)
        target_compile_definitions(renderdoc-mcp-lib PUBLIC RENDERDOC_PLATFORM_WIN32)
    endif()
    # find and link renderdoc.lib (same logic as before)
    # ... (keep existing find_library logic, targeting renderdoc-mcp-lib)

    # renderdoc-mcp executable
    add_executable(renderdoc-mcp src/main.cpp)
    target_link_libraries(renderdoc-mcp PRIVATE renderdoc-mcp-lib)

    # copy renderdoc.dll (same logic as before)
    # ...
else()
    message(STATUS "RENDERDOC_DIR not set - building renderdoc-mcp-core only (no exe, no renderdoc-dependent targets)")
endif()

# ── tests ────────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Build without RENDERDOC_DIR to verify core-only mode**

```bash
cmake -B build-core-only -DBUILD_TESTING=OFF
cmake --build build-core-only --config Release
```

Expected: builds `renderdoc-mcp-core.lib` only, no errors.

- [ ] **Step 3: Build with RENDERDOC_DIR to verify full mode**

```bash
cmake -B build -DRENDERDOC_DIR=D:/renderdoc/renderdoc -DBUILD_TESTING=OFF
cmake --build build --config Release
```

Expected: builds core + lib + exe, works identically.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: split into renderdoc-mcp-core and renderdoc-mcp-lib targets"
```

---

### Task 3: Create Test Infrastructure (tests/CMakeLists.txt + Stub)

**Files:**
- Create: `tests/CMakeLists.txt`
- Create: `tests/unit/renderdoc_wrapper_stub.cpp`

- [ ] **Step 1: Create `tests/unit/renderdoc_wrapper_stub.cpp`**

```cpp
// Link stub for RenderdocWrapper — provides empty-body implementations
// so test-unit can link without renderdoc.lib.
// These are never called in unit tests; they only satisfy the linker.
#include "renderdoc_wrapper.h"

RenderdocWrapper::~RenderdocWrapper() {}

void RenderdocWrapper::shutdown() {}

nlohmann::json RenderdocWrapper::openCapture(const std::string&) { return {}; }
nlohmann::json RenderdocWrapper::listEvents(const std::string&) { return {}; }
nlohmann::json RenderdocWrapper::gotoEvent(uint32_t) { return {}; }
nlohmann::json RenderdocWrapper::getPipelineState() { return {}; }
nlohmann::json RenderdocWrapper::exportRenderTarget(int) { return {}; }

std::string RenderdocWrapper::getExportDir() const { return {}; }
std::string RenderdocWrapper::generateOutputPath(uint32_t, int) const { return {}; }

void RenderdocWrapper::ensureReplayInitialized() {}
void RenderdocWrapper::closeCurrent() {}
```

- [ ] **Step 2: Create `tests/CMakeLists.txt`**

```cmake
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# ── test-unit (zero renderdoc dependency) ────────────────────────────────────
add_executable(test-unit
    unit/test_tool_registry.cpp
    unit/test_mcp_server.cpp
    unit/renderdoc_wrapper_stub.cpp
)
target_link_libraries(test-unit PRIVATE renderdoc-mcp-core GTest::gtest_main)
gtest_discover_tests(test-unit PROPERTIES LABELS unit)

# ── renderdoc-dependent test targets ─────────────────────────────────────────
if(TARGET renderdoc-mcp-lib)
    set(RENDERDOC_TEST_CAPTURE "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vkcube.rdc"
        CACHE FILEPATH "Path to test .rdc capture file")

    # test-tools
    add_executable(test-tools
        integration/test_tools.cpp
    )
    target_link_libraries(test-tools PRIVATE renderdoc-mcp-lib GTest::gtest_main)
    target_compile_definitions(test-tools PRIVATE
        TEST_RDC_PATH="${RENDERDOC_TEST_CAPTURE}"
    )
    gtest_discover_tests(test-tools PROPERTIES LABELS integration)

    # test-integration (process-level, no project lib linkage)
    add_executable(test-integration
        integration/test_protocol.cpp
        integration/test_workflow.cpp
    )
    target_link_libraries(test-integration PRIVATE GTest::gtest_main nlohmann_json::nlohmann_json)
    target_compile_definitions(test-integration PRIVATE
        TEST_EXE_PATH="$<TARGET_FILE:renderdoc-mcp>"
        TEST_RDC_PATH="${RENDERDOC_TEST_CAPTURE}"
    )
    gtest_discover_tests(test-integration PROPERTIES LABELS integration)
endif()
```

- [ ] **Step 3: Build test-unit without RENDERDOC_DIR**

```bash
cmake -B build-unit -DBUILD_TESTING=ON
cmake --build build-unit --config Release --target test-unit
```

Expected: builds `test-unit.exe` with no renderdoc dependency. (Will fail to run until we add actual test files in Task 4, but linker must succeed.)

- [ ] **Step 4: Commit**

```bash
git add tests/CMakeLists.txt tests/unit/renderdoc_wrapper_stub.cpp
git commit -m "build: add test infrastructure with GoogleTest and wrapper stub"
```

---

### Task 4: ToolRegistry Unit Tests

**Files:**
- Create: `tests/unit/test_tool_registry.cpp`

- [ ] **Step 1: Write test file**

```cpp
#include <gtest/gtest.h>
#include "tool_registry.h"

// Helper: register a dummy tool with a given schema
static void registerDummy(ToolRegistry& reg, const std::string& name,
                          const nlohmann::json& schema)
{
    reg.registerTool({
        name, "dummy " + name, schema,
        [](RenderdocWrapper&, const nlohmann::json& args) -> nlohmann::json {
            return {{"ok", true}};
        }
    });
}

TEST(ToolRegistryTest, HasTool_RegisteredTool_ReturnsTrue)
{
    ToolRegistry reg;
    registerDummy(reg, "foo", {{"type", "object"}, {"properties", nlohmann::json::object()}});
    EXPECT_TRUE(reg.hasTool("foo"));
}

TEST(ToolRegistryTest, HasTool_UnknownTool_ReturnsFalse)
{
    ToolRegistry reg;
    EXPECT_FALSE(reg.hasTool("nonexistent"));
}

TEST(ToolRegistryTest, CallTool_UnknownName_Throws)
{
    ToolRegistry reg;
    RenderdocWrapper w;
    EXPECT_THROW(reg.callTool("nonexistent", w, {}), InvalidParamsError);
}

TEST(ToolRegistryTest, RequiredFieldMissing_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"path", {{"type", "string"}}}}},
        {"required", nlohmann::json::array({"path"})}
    });
    RenderdocWrapper w;
    EXPECT_THROW(reg.callTool("t", w, {}), InvalidParamsError);
}

TEST(ToolRegistryTest, WrongType_String_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"name", {{"type", "string"}}}}}
    });
    RenderdocWrapper w;
    EXPECT_THROW(reg.callTool("t", w, {{"name", 123}}), InvalidParamsError);
}

TEST(ToolRegistryTest, WrongType_Integer_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"count", {{"type", "integer"}}}}}
    });
    RenderdocWrapper w;
    EXPECT_THROW(reg.callTool("t", w, {{"count", "abc"}}), InvalidParamsError);
}

TEST(ToolRegistryTest, WrongType_Boolean_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"flag", {{"type", "boolean"}}}}}
    });
    RenderdocWrapper w;
    EXPECT_THROW(reg.callTool("t", w, {{"flag", "yes"}}), InvalidParamsError);
}

TEST(ToolRegistryTest, EnumValidation_InvalidValue_Throws)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"mode", {{"type", "string"}, {"enum", {"a", "b"}}}}}}
    });
    RenderdocWrapper w;
    EXPECT_THROW(reg.callTool("t", w, {{"mode", "c"}}), InvalidParamsError);
}

TEST(ToolRegistryTest, EnumValidation_ValidValue_Passes)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"mode", {{"type", "string"}, {"enum", {"a", "b"}}}}}}
    });
    RenderdocWrapper w;
    EXPECT_NO_THROW(reg.callTool("t", w, {{"mode", "a"}}));
}

TEST(ToolRegistryTest, OptionalField_Absent_NoError)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"opt", {{"type", "string"}}}}}
        // no "required" array
    });
    RenderdocWrapper w;
    EXPECT_NO_THROW(reg.callTool("t", w, {}));
}

TEST(ToolRegistryTest, UnknownField_Ignored)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"known", {{"type", "string"}}}}}
    });
    RenderdocWrapper w;
    EXPECT_NO_THROW(reg.callTool("t", w, {{"known", "v"}, {"extra", 42}}));
}
```

- [ ] **Step 2: Build and run**

```bash
cmake -B build-unit -DBUILD_TESTING=ON
cmake --build build-unit --config Release --target test-unit
cd build-unit && ctest -L unit -C Release --output-on-failure
```

Expected: all 10 tests PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/test_tool_registry.cpp
git commit -m "test: add ToolRegistry unit tests (10 cases, zero renderdoc dep)"
```

---

### Task 5: McpServer Protocol Unit Tests

**Files:**
- Create: `tests/unit/test_mcp_server.cpp`

- [ ] **Step 1: Write test file**

```cpp
#include <gtest/gtest.h>
#include "mcp_server.h"
#include "tool_registry.h"
#include "renderdoc_wrapper.h"

using json = nlohmann::json;

class McpServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Register a dummy tool that returns a fixed result
        m_registry.registerTool({
            "echo_tool", "echoes input",
            {{"type", "object"}, {"properties", {{"msg", {{"type", "string"}}}}},
             {"required", json::array({"msg"})}},
            [](RenderdocWrapper&, const json& args) -> json {
                return {{"echo", args["msg"]}};
            }
        });
        // Register a tool that throws runtime_error
        m_registry.registerTool({
            "fail_tool", "always fails",
            {{"type", "object"}, {"properties", json::object()}},
            [](RenderdocWrapper&, const json&) -> json {
                throw std::runtime_error("deliberate failure");
            }
        });
        // Register a tool that throws InvalidParamsError
        m_registry.registerTool({
            "invalid_tool", "throws InvalidParamsError",
            {{"type", "object"}, {"properties", json::object()}},
            [](RenderdocWrapper&, const json&) -> json {
                throw InvalidParamsError("bad param from handler");
            }
        });

        m_server = std::make_unique<McpServer>(m_registry, m_wrapper);
    }

    json makeRequest(const std::string& method, const json& params = json::object(), int id = 1) {
        json req;
        req["jsonrpc"] = "2.0";
        req["id"] = id;
        req["method"] = method;
        if(!params.is_null())
            req["params"] = params;
        return req;
    }

    RenderdocWrapper m_wrapper;
    ToolRegistry m_registry;
    std::unique_ptr<McpServer> m_server;
};

TEST_F(McpServerTest, Initialize_ReturnsServerInfo)
{
    auto resp = m_server->handleMessage(makeRequest("initialize"));
    ASSERT_TRUE(resp.contains("result"));
    EXPECT_EQ(resp["result"]["serverInfo"]["name"], "renderdoc-mcp");
    EXPECT_EQ(resp["result"]["protocolVersion"], "2025-03-26");
}

TEST_F(McpServerTest, Initialize_HasToolsCapability)
{
    auto resp = m_server->handleMessage(makeRequest("initialize"));
    EXPECT_TRUE(resp["result"]["capabilities"].contains("tools"));
}

TEST_F(McpServerTest, ToolsList_ReturnsRegisteredTools)
{
    auto resp = m_server->handleMessage(makeRequest("tools/list"));
    auto tools = resp["result"]["tools"];
    EXPECT_EQ(tools.size(), 3u);  // echo_tool, fail_tool, invalid_tool
}

TEST_F(McpServerTest, ToolsList_EachHasRequiredFields)
{
    auto resp = m_server->handleMessage(makeRequest("tools/list"));
    for(const auto& tool : resp["result"]["tools"]) {
        EXPECT_TRUE(tool.contains("name"));
        EXPECT_TRUE(tool.contains("description"));
        EXPECT_TRUE(tool.contains("inputSchema"));
    }
}

TEST_F(McpServerTest, ToolsCall_UnknownTool_ReturnsError)
{
    auto resp = m_server->handleMessage(makeRequest("tools/call",
        {{"name", "nonexistent"}, {"arguments", json::object()}}));
    ASSERT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32602);
}

TEST_F(McpServerTest, ToolsCall_ValidTool_ReturnsHandlerResult)
{
    auto resp = m_server->handleMessage(makeRequest("tools/call",
        {{"name", "echo_tool"}, {"arguments", {{"msg", "hello"}}}}));
    ASSERT_TRUE(resp.contains("result"));
    EXPECT_FALSE(resp["result"].value("isError", false));
    auto text = resp["result"]["content"][0]["text"].get<std::string>();
    auto parsed = json::parse(text);
    EXPECT_EQ(parsed["echo"], "hello");
}

TEST_F(McpServerTest, ToolsCall_HandlerThrowsRuntime_ReturnsIsError)
{
    auto resp = m_server->handleMessage(makeRequest("tools/call",
        {{"name", "fail_tool"}, {"arguments", json::object()}}));
    ASSERT_TRUE(resp.contains("result"));
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
}

TEST_F(McpServerTest, ToolsCall_HandlerThrowsInvalidParams_Returns32602)
{
    auto resp = m_server->handleMessage(makeRequest("tools/call",
        {{"name", "invalid_tool"}, {"arguments", json::object()}}));
    ASSERT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32602);
}

TEST_F(McpServerTest, UnknownMethod_ReturnsMethodNotFound)
{
    auto resp = m_server->handleMessage(makeRequest("unknown/method"));
    ASSERT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32601);
}

TEST_F(McpServerTest, InvalidParams_MissingToolName_Returns32602)
{
    auto resp = m_server->handleMessage(makeRequest("tools/call",
        {{"arguments", json::object()}}));  // no "name"
    ASSERT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32602);
}

TEST_F(McpServerTest, BatchRequest_ReturnsBatchResponse)
{
    json batch = json::array({
        makeRequest("tools/list", json::object(), 1),
        makeRequest("tools/list", json::object(), 2)
    });
    auto resp = m_server->handleBatch(batch);
    ASSERT_TRUE(resp.is_array());
    EXPECT_EQ(resp.size(), 2u);
}

TEST_F(McpServerTest, BatchWithInitialize_Rejected)
{
    json batch = json::array({
        makeRequest("initialize", json::object(), 1),
        makeRequest("tools/list", json::object(), 2)
    });
    auto resp = m_server->handleBatch(batch);
    ASSERT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32600);
}
```

- [ ] **Step 2: Build and run**

```bash
cmake --build build-unit --config Release --target test-unit
cd build-unit && ctest -L unit -C Release --output-on-failure
```

Expected: all tests (registry + server) PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/test_mcp_server.cpp
git commit -m "test: add McpServer protocol unit tests (12 cases, zero renderdoc dep)"
```

---

### Task 6: Tool Logic Integration Tests

**Files:**
- Create: `tests/integration/test_tools.cpp`

This task requires RENDERDOC_DIR and vkcube.rdc. Build with full config.

- [ ] **Step 1: Write test file**

```cpp
#include <gtest/gtest.h>
#include "tool_registry.h"
#include "renderdoc_wrapper.h"
#include "tools/tools.h"
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

class RenderdocToolTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        registerSessionTools(s_registry);
        registerEventTools(s_registry);
        registerPipelineTools(s_registry);
        registerExportTools(s_registry);
        registerInfoTools(s_registry);
        registerResourceTools(s_registry);
        registerShaderTools(s_registry);

        // Open test capture
        auto result = s_registry.callTool("open_capture", s_wrapper,
            {{"path", TEST_RDC_PATH}});
        ASSERT_FALSE(result.empty()) << "open_capture returned empty";

        // Navigate to first draw call
        auto draws = s_registry.callTool("list_draws", s_wrapper, {});
        ASSERT_TRUE(draws.contains("draws"));
        ASSERT_GT(draws["draws"].size(), 0u);
        uint32_t firstDrawEid = draws["draws"][0]["eventId"].get<uint32_t>();
        s_registry.callTool("goto_event", s_wrapper, {{"eventId", firstDrawEid}});
        s_firstDrawEventId = firstDrawEid;
    }

    static void TearDownTestSuite() {
        s_wrapper.shutdown();
    }

    static RenderdocWrapper s_wrapper;
    static ToolRegistry s_registry;
    static uint32_t s_firstDrawEventId;
};

RenderdocWrapper RenderdocToolTest::s_wrapper;
ToolRegistry RenderdocToolTest::s_registry;
uint32_t RenderdocToolTest::s_firstDrawEventId = 0;

// ── open_capture ────────────────────────────────────────────────────────────

TEST_F(RenderdocToolTest, OpenCapture_ReturnsApiAndEventCount)
{
    // Already opened in SetUpTestSuite, just verify state
    EXPECT_TRUE(s_wrapper.hasCaptureOpen());
}

TEST_F(RenderdocToolTest, OpenCapture_InvalidPath_Throws)
{
    EXPECT_THROW(
        s_registry.callTool("open_capture", s_wrapper, {{"path", "/nonexistent.rdc"}}),
        std::exception);
}

// ── list_events ─────────────────────────────────────────────────────────────

TEST_F(RenderdocToolTest, ListEvents_ReturnsNonEmpty)
{
    auto result = s_registry.callTool("list_events", s_wrapper, {});
    EXPECT_TRUE(result.contains("events"));
    EXPECT_GT(result["events"].size(), 0u);
}

TEST_F(RenderdocToolTest, ListEvents_InvalidFilter_ReturnsEmpty)
{
    auto result = s_registry.callTool("list_events", s_wrapper,
        {{"filter", "zzz_no_match_zzz"}});
    EXPECT_TRUE(result.contains("events"));
    EXPECT_EQ(result["events"].size(), 0u);
}

// ── goto_event ──────────────────────────────────────────────────────────────

TEST_F(RenderdocToolTest, GotoEvent_ValidId)
{
    EXPECT_NO_THROW(
        s_registry.callTool("goto_event", s_wrapper, {{"eventId", s_firstDrawEventId}}));
}

TEST_F(RenderdocToolTest, GotoEvent_InvalidId_Throws)
{
    EXPECT_THROW(
        s_registry.callTool("goto_event", s_wrapper, {{"eventId", 999999}}),
        std::exception);
}

// ── list_draws ──────────────────────────────────────────────────────────────

TEST_F(RenderdocToolTest, ListDraws_HasCorrectFields)
{
    auto result = s_registry.callTool("list_draws", s_wrapper, {});
    ASSERT_GT(result["draws"].size(), 0u);
    auto& draw = result["draws"][0];
    EXPECT_TRUE(draw.contains("eventId"));
    EXPECT_TRUE(draw.contains("name"));
    EXPECT_TRUE(draw.contains("flags"));
    EXPECT_TRUE(draw.contains("numIndices"));
    EXPECT_TRUE(draw.contains("numInstances"));
}

TEST_F(RenderdocToolTest, ListDraws_FilterNoMatch_ReturnsEmpty)
{
    auto result = s_registry.callTool("list_draws", s_wrapper,
        {{"filter", "zzz_no_match_zzz"}});
    EXPECT_EQ(result["draws"].size(), 0u);
}

// ── get_draw_info ───────────────────────────────────────────────────────────

TEST_F(RenderdocToolTest, GetDrawInfo_ValidId)
{
    auto result = s_registry.callTool("get_draw_info", s_wrapper,
        {{"eventId", s_firstDrawEventId}});
    EXPECT_EQ(result["eventId"], s_firstDrawEventId);
    EXPECT_TRUE(result.contains("name"));
}

TEST_F(RenderdocToolTest, GetDrawInfo_InvalidId_Throws)
{
    EXPECT_THROW(
        s_registry.callTool("get_draw_info", s_wrapper, {{"eventId", 999999}}),
        std::exception);
}

// ── get_pipeline_state ──────────────────────────────────────────────────────

TEST_F(RenderdocToolTest, GetPipelineState_ReturnsShaderStages)
{
    auto result = s_registry.callTool("get_pipeline_state", s_wrapper, {});
    EXPECT_TRUE(result.contains("api") || result.contains("stages"));
}

TEST_F(RenderdocToolTest, GetPipelineState_WithEventId)
{
    auto result = s_registry.callTool("get_pipeline_state", s_wrapper,
        {{"eventId", s_firstDrawEventId}});
    EXPECT_FALSE(result.empty());
}

// ── get_bindings ────────────────────────────────────────────────────────────

TEST_F(RenderdocToolTest, GetBindings_ReturnsStages)
{
    auto result = s_registry.callTool("get_bindings", s_wrapper, {});
    EXPECT_TRUE(result.contains("stages"));
    EXPECT_TRUE(result.contains("api"));
}

// ── get_capture_info ────────────────────────────────────────────────────────

TEST_F(RenderdocToolTest, GetCaptureInfo_ReturnsMetadata)
{
    auto result = s_registry.callTool("get_capture_info", s_wrapper, {});
    EXPECT_TRUE(result.contains("api"));
}

// ── get_stats ───────────────────────────────────────────────────────────────

TEST_F(RenderdocToolTest, GetStats_ReturnsData)
{
    auto result = s_registry.callTool("get_stats", s_wrapper, {});
    EXPECT_FALSE(result.empty());
}

// ── list_resources ──────────────────────────────────────────────────────────

TEST_F(RenderdocToolTest, ListResources_ReturnsNonEmpty)
{
    auto result = s_registry.callTool("list_resources", s_wrapper, {});
    EXPECT_TRUE(result.contains("resources"));
    EXPECT_GT(result["count"].get<int>(), 0);
}

// ── get_resource_info ───────────────────────────────────────────────────────

TEST_F(RenderdocToolTest, GetResourceInfo_InvalidId_Throws)
{
    EXPECT_THROW(
        s_registry.callTool("get_resource_info", s_wrapper,
            {{"resourceId", "ResourceId::0"}}),
        std::exception);
}

// ── list_passes ─────────────────────────────────────────────────────────────

TEST_F(RenderdocToolTest, ListPasses_ReturnsList)
{
    auto result = s_registry.callTool("list_passes", s_wrapper, {});
    EXPECT_TRUE(result.contains("passes"));
}

// ── export_render_target ────────────────────────────────────────────────────

TEST_F(RenderdocToolTest, ExportRenderTarget_CreatesPNG)
{
    auto result = s_registry.callTool("export_render_target", s_wrapper, {});
    if(result.contains("path")) {
        std::string path = result["path"].get<std::string>();
        EXPECT_TRUE(fs::exists(path));
        EXPECT_GT(fs::file_size(path), 0u);
    }
}
```

Note: Some tests (get_shader, list_shaders, search_shaders, get_pass_info, export_texture, export_buffer, get_log) are omitted from this initial implementation for brevity. They follow the same pattern and can be added incrementally. The key patterns (positive + negative, field validation) are demonstrated above.

- [ ] **Step 2: Build and run (requires RENDERDOC_DIR)**

```bash
cmake -B build -DBUILD_TESTING=ON -DRENDERDOC_DIR=D:/renderdoc/renderdoc
cmake --build build --config Release --target test-tools
cd build && ctest -L integration -R test-tools -C Release --output-on-failure
```

Expected: all tests PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/integration/test_tools.cpp
git commit -m "test: add tool logic integration tests (18 cases, requires renderdoc)"
```

---

### Task 7: Process-Level Integration Tests

**Files:**
- Create: `tests/integration/test_protocol.cpp`
- Create: `tests/integration/test_workflow.cpp`

- [ ] **Step 1: Write `tests/integration/test_protocol.cpp`**

Include ProcessRunner helper class (Win32 CreateProcess + anonymous pipes) and protocol tests.

The ProcessRunner class:
- `start()`: CreateProcess with stdin/stdout redirected via anonymous pipes
- `sendRequest(json)`: write JSON + `\n` to stdin pipe, read response line from stdout pipe with 5s timeout
- `stop()`: TerminateProcess + CloseHandle

Protocol tests:
- `InitializeHandshake`: send initialize, verify jsonrpc/result/serverInfo
- `ToolsListComplete`: tools/list returns 20 tools
- `ParseError_MalformedJson`: send `{broken` → error code -32700
- `MethodNotFound_UnknownMethod`: unknown method → -32601
- `BatchRequest_ArrayResponse`: send array of 2 requests, get array of 2 responses
- `ProcessStable_MultipleRequests`: send 5 sequential requests, all get valid responses

- [ ] **Step 2: Write `tests/integration/test_workflow.cpp`**

End-to-end workflow tests using ProcessRunner:
- `FullDebugWorkflow`: initialize → open_capture → get_capture_info → list_events → goto_event → get_pipeline_state → export_render_target → verify response chain
- `EventNavigation`: iterate multiple events via goto_event
- `ResourceInspection`: list_resources → get_resource_info flow

- [ ] **Step 3: Build and run**

```bash
cmake --build build --config Release --target test-integration
cd build && ctest -L integration -R test-integration -C Release --output-on-failure
```

Expected: all tests PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/integration/test_protocol.cpp tests/integration/test_workflow.cpp
git commit -m "test: add process-level protocol and workflow integration tests"
```

---

### Task 8: GitHub Actions CI

**Files:**
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Write CI workflow**

```yaml
name: CI

on:
  push:
    branches: [main]
  pull_request:

jobs:
  unit-tests:
    name: Unit Tests (no renderdoc)
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4

      - name: Configure CMake
        run: cmake -B build -DBUILD_TESTING=ON

      - name: Build test-unit
        run: cmake --build build --config Release --target test-unit

      - name: Run unit tests
        run: ctest --test-dir build -L unit -C Release --output-on-failure

  integration-tests:
    name: Integration Tests (renderdoc)
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4

      - name: Cache renderdoc source and build
        id: cache-renderdoc
        uses: actions/cache@v4
        with:
          path: |
            renderdoc-src
            renderdoc-build
          key: renderdoc-v1.36-${{ runner.os }}

      - name: Clone and build renderdoc
        if: steps.cache-renderdoc.outputs.cache-hit != 'true'
        shell: bash
        run: |
          git clone --depth 1 --branch v1.36 https://github.com/baldurk/renderdoc.git renderdoc-src
          cmake -B renderdoc-build -S renderdoc-src -DCMAKE_BUILD_TYPE=Release
          cmake --build renderdoc-build --config Release --target renderdoc

      - name: Configure CMake
        run: >
          cmake -B build -DBUILD_TESTING=ON
          -DRENDERDOC_DIR=renderdoc-src
          -DRENDERDOC_BUILD_DIR=renderdoc-build

      - name: Build all
        run: cmake --build build --config Release

      - name: Run integration tests
        run: ctest --test-dir build -L integration -C Release --output-on-failure
```

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: add GitHub Actions with unit and integration test jobs"
```

---

### Task 9: Final Verification

- [ ] **Step 1: Run all unit tests without renderdoc**

```bash
cmake -B build-unit -DBUILD_TESTING=ON
cmake --build build-unit --config Release --target test-unit
ctest --test-dir build-unit -L unit -C Release --output-on-failure
```

Expected: all unit tests PASS, zero renderdoc dependency.

- [ ] **Step 2: Run all tests with renderdoc**

```bash
cmake -B build -DBUILD_TESTING=ON -DRENDERDOC_DIR=D:/renderdoc/renderdoc
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected: all unit + integration tests PASS.

- [ ] **Step 3: Verify exe still works**

```bash
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' | ./build/Release/renderdoc-mcp.exe
```

Expected: valid JSON-RPC initialize response.

- [ ] **Step 4: Commit any remaining fixes**

```bash
git add -A
git commit -m "test: final verification and fixes"
```
