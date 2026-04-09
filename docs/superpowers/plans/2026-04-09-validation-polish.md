# Validation & Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve error handling, parameter validation, serialization consistency, and code documentation across the MCP server.

**Architecture:** Four independent fixes touching serialization, tools, and constants. No new modules or architectural changes.

**Tech Stack:** C++17, nlohmann/json, Google Test

---

### Task 1: ResourceId Parse Error Wrapping

**Files:**
- Modify: `src/mcp/serialization.cpp:1-18`
- Modify: `tests/unit/test_serialization.cpp:13-17`

- [ ] **Step 1: Update the existing test to expect CoreError instead of std::invalid_argument**

In `tests/unit/test_serialization.cpp`, replace the `InvalidFormat` test:

```cpp
TEST(ResourceIdSerialization, InvalidFormat) {
    EXPECT_THROW(mcp::parseResourceId("123"), core::CoreError);
    EXPECT_THROW(mcp::parseResourceId("ResourceId:"), core::CoreError);
    EXPECT_THROW(mcp::parseResourceId(""), core::CoreError);
}
```

Add a new test for numeric overflow/invalid number after the prefix:

```cpp
TEST(ResourceIdSerialization, InvalidNumber) {
    EXPECT_THROW(mcp::parseResourceId("ResourceId::abc"), core::CoreError);
    EXPECT_THROW(mcp::parseResourceId("ResourceId::-1"), core::CoreError);
    EXPECT_THROW(mcp::parseResourceId("ResourceId::"), core::CoreError);
}
```

Add the include at the top of the test file (after line 2):

```cpp
#include "core/errors.h"
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd D:/renderdoc/renderdoc-mcp/build && cmake --build . --target test-unit --config Release 2>&1 | tail -5 && ctest -R test-unit -C Release --output-on-failure 2>&1 | tail -20`

Expected: FAIL — `parseResourceId` still throws `std::invalid_argument`, not `CoreError`.

- [ ] **Step 3: Implement CoreError wrapping in parseResourceId**

In `src/mcp/serialization.cpp`, add the include (after line 2):

```cpp
#include "core/errors.h"
```

Replace the `parseResourceId` function (lines 13-18):

```cpp
core::ResourceId parseResourceId(const std::string& str) {
    const std::string prefix = "ResourceId::";
    if (str.rfind(prefix, 0) != 0)
        throw core::CoreError(core::CoreError::Code::InvalidResourceId,
                              "Invalid ResourceId format '" + str + "', expected 'ResourceId::<number>'");
    try {
        return std::stoull(str.substr(prefix.size()));
    } catch (...) {
        throw core::CoreError(core::CoreError::Code::InvalidResourceId,
                              "Invalid ResourceId number in '" + str + "'");
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd D:/renderdoc/renderdoc-mcp/build && cmake --build . --target test-unit --config Release 2>&1 | tail -5 && ctest -R test-unit -C Release --output-on-failure 2>&1 | tail -20`

Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add src/mcp/serialization.cpp tests/unit/test_serialization.cpp
git commit -m "fix: wrap ResourceId parse errors in CoreError for consistent error handling"
```

---

### Task 2: assert_pixel Array Length Validation

**Files:**
- Modify: `src/mcp/tools/assertion_tools.cpp:70-81`

- [ ] **Step 1: Add array length validation before the unsafe copy**

In `src/mcp/tools/assertion_tools.cpp`, in the `assert_pixel` handler lambda (around line 75-76), replace:

```cpp
            auto expectedVec = args["expected"].get<std::vector<float>>();
            float expected[4] = {expectedVec[0], expectedVec[1], expectedVec[2], expectedVec[3]};
```

with:

```cpp
            auto expectedVec = args["expected"].get<std::vector<float>>();
            if (expectedVec.size() != 4)
                throw InvalidParamsError("'expected' must be an array of exactly 4 floats [R, G, B, A], got " + std::to_string(expectedVec.size()) + " elements");
            float expected[4] = {expectedVec[0], expectedVec[1], expectedVec[2], expectedVec[3]};
```

- [ ] **Step 2: Build to verify compilation**

Run: `cd D:/renderdoc/renderdoc-mcp/build && cmake --build . --config Release 2>&1 | tail -5`

Expected: BUILD SUCCEEDED

- [ ] **Step 3: Run existing tests to verify no regressions**

Run: `cd D:/renderdoc/renderdoc-mcp/build && ctest -R test-unit -C Release --output-on-failure 2>&1 | tail -10`

Expected: ALL PASS (assert_pixel is integration-tested, unit tests unaffected)

- [ ] **Step 4: Commit**

```bash
git add src/mcp/tools/assertion_tools.cpp
git commit -m "fix: validate assert_pixel expected array length to prevent out-of-bounds access"
```

---

### Task 3: Serialization Output Consistency (depth field)

**Files:**
- Modify: `src/mcp/serialization.cpp:281-284`
- Modify: `tests/unit/test_serialization.cpp:159-165`

- [ ] **Step 1: Update the NullDepth test to expect omission instead of null**

In `tests/unit/test_serialization.cpp`, replace the `NullDepth` test (lines 159-165):

```cpp
TEST(PixelModificationSerialization, NullDepth) {
    core::PixelModification mod;
    mod.depth = std::nullopt;

    auto j = mcp::to_json(mod);
    EXPECT_FALSE(j.contains("depth"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd D:/renderdoc/renderdoc-mcp/build && cmake --build . --target test-unit --config Release 2>&1 | tail -5 && ctest -R test-unit -C Release --output-on-failure 2>&1 | tail -20`

Expected: FAIL — `depth` is still set to `null`, so `j.contains("depth")` returns `true`.

- [ ] **Step 3: Fix the serialization to omit depth when absent**

In `src/mcp/serialization.cpp`, replace lines 281-284:

```cpp
    if (mod.depth.has_value())
        j["depth"] = *mod.depth;
    else
        j["depth"] = nullptr;
```

with:

```cpp
    if (mod.depth.has_value())
        j["depth"] = *mod.depth;
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd D:/renderdoc/renderdoc-mcp/build && cmake --build . --target test-unit --config Release 2>&1 | tail -5 && ctest -R test-unit -C Release --output-on-failure 2>&1 | tail -20`

Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add src/mcp/serialization.cpp tests/unit/test_serialization.cpp
git commit -m "fix: omit depth field when absent instead of setting null for consistency"
```

---

### Task 4: Constants Documentation

**Files:**
- Modify: `src/core/constants.h`

- [ ] **Step 1: Add source comments to all constants**

Replace the entire content of `src/core/constants.h`:

```cpp
#pragma once
#include <cstdint>

namespace renderdoc::core {

// D3D11/12 maximum simultaneous render targets; Vulkan guarantees at least 4,
// but 8 matches the D3D limit and covers all common hardware.
constexpr int kMaxRenderTargets = 8;

// Default page size for list_draws tool output.
// Project choice: balances detail vs. MCP response size for AI consumption.
constexpr uint32_t kDefaultDrawLimit = 1000;

// Default max results for search_shaders tool.
// Project choice: keeps response size manageable while covering typical captures.
constexpr uint32_t kDefaultShaderSearchLimit = 50;

// Number of histogram buckets for get_texture_stats tool.
// Matches 8-bit value range (0-255), the standard resolution for texture histograms.
constexpr uint32_t kHistogramBucketCount = 256;

// Maximum float/uint/int components stored per shader debug variable.
// Covers mat4 (16 floats), the largest common HLSL/GLSL type.
constexpr uint32_t kMaxDebugVarComponents = 16;

} // namespace renderdoc::core
```

- [ ] **Step 2: Build to verify compilation**

Run: `cd D:/renderdoc/renderdoc-mcp/build && cmake --build . --config Release 2>&1 | tail -5`

Expected: BUILD SUCCEEDED

- [ ] **Step 3: Run tests to verify no regressions**

Run: `cd D:/renderdoc/renderdoc-mcp/build && ctest -R test-unit -C Release --output-on-failure 2>&1 | tail -10`

Expected: ALL PASS

- [ ] **Step 4: Commit**

```bash
git add src/core/constants.h
git commit -m "docs: add source/rationale comments to all constants"
```
