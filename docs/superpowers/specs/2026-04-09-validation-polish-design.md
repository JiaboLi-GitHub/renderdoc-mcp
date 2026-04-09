# Validation & Polish Design Spec

**Date:** 2026-04-09
**Scope:** 4 targeted improvements to error handling, validation, serialization consistency, and code documentation.

## 1. Tool-Layer Parameter Boundary Validation

### Problem

Most parameters are already validated in the core layer (eventId, coordinates, targetIndex, resourceId existence). However, `assert_pixel`'s `expected` array has no length check — passing fewer than 4 elements causes undefined behavior (out-of-bounds array access).

### Solution

Add array length validation in `src/mcp/tools/assertion_tools.cpp` for `assert_pixel`:

- Check `expectedVec.size() == 4` before copying to `float[4]`
- Throw `InvalidParamsError(...)` (MCP protocol -32602 error) with a clear message if length != 4. This is a parameter format issue, not a core logic error, so `InvalidParamsError` is appropriate — no new error code needed.

No other tools need MCP-layer validation — core already covers eventId, coordinates, targetIndex, and resourceId existence.

### Files Changed

- `src/mcp/tools/assertion_tools.cpp` — add array length check

## 2. ResourceId Parse Error Wrapping

### Problem

`parseResourceId()` in `src/mcp/serialization.cpp` can throw `std::invalid_argument` or `std::out_of_range` from `std::stoull()`. These raw C++ stdlib exceptions:
- Expose implementation details to the user
- Bypass the `CoreError` system
- Produce unhelpful error messages

### Solution

Wrap all exceptions in `parseResourceId()` with `CoreError(Code::InvalidResourceId, ...)`:

```cpp
core::ResourceId parseResourceId(const std::string& str) {
    const std::string prefix = "ResourceId::";
    if (str.rfind(prefix, 0) != 0)
        throw CoreError(CoreError::Code::InvalidResourceId,
                        "Invalid ResourceId format '" + str + "', expected 'ResourceId::<number>'");
    try {
        return std::stoull(str.substr(prefix.size()));
    } catch (...) {
        throw CoreError(CoreError::Code::InvalidResourceId,
                        "Invalid ResourceId number in '" + str + "'");
    }
}
```

### Files Changed

- `src/mcp/serialization.cpp` — rewrite `parseResourceId()` to use CoreError
- `src/mcp/serialization.cpp` — add `#include "core/errors.h"` if not present

## 3. Serialization Output Consistency

### Problem

Optional fields in `to_json()` functions use an inconsistent pattern. Most use the omission pattern (`if (val) j["key"] = *val;`), but there may be stray `null` assignments. The JSON output shape should be predictable for consumers.

### Solution

**Convention:** Optional fields are **omitted** when empty (not set to `null`). This is already the dominant pattern.

**Specific locations using `nullptr` that need fixing:**

1. **Line ~284** — `to_json(PixelModification)`: `j["depth"] = nullptr` when `mod.depth` has no value. Change to omission pattern.
2. **Lines ~494-495** — `to_json(DrawDiffRow)`: `j["a"]` and `j["b"]` set to `nullptr` when absent. These are semantically meaningful (A/B sides of a diff comparison). **Keep as `null`** — diff rows always have both fields present to indicate "no match on this side."
3. **Lines ~534-539** — `to_json(PassDiffRow)`: 6 stat fields (`drawsA/B`, `trianglesA/B`, `dispatchesA/B`) set to `nullptr`. Same reasoning as DrawDiffRow — **keep as `null`** for diff row symmetry.

**Net change:** Only fix line 284 (`depth` field). The diff row `nullptr` values are intentional and semantically correct (they represent "absent side" in a comparison, not "unknown").

### Files Changed

- `src/mcp/serialization.cpp` — fix any inconsistent optional field handling

## 4. Constants Documentation

### Problem

`src/core/constants.h` defines numeric constants without explaining their origin. Maintainers cannot tell if a value is a RenderDoc API limit, a D3D/Vulkan spec value, or an arbitrary project choice.

### Solution

Add a one-line comment to each constant explaining its source:

```cpp
// D3D11/12 maximum simultaneous render targets (also covers Vulkan typical limit)
constexpr int kMaxRenderTargets = 8;

// Default page size for list_draws results (project choice, balances detail vs. response size)
constexpr uint32_t kDefaultDrawLimit = 1000;

// Default max results for search_shaders (project choice)
constexpr uint32_t kDefaultShaderSearchLimit = 50;

// Standard histogram resolution for texture statistics (covers 8-bit value range)
constexpr uint32_t kHistogramBucketCount = 256;

// Maximum RGBA/vector components for shader debug variable display
constexpr uint32_t kMaxDebugVarComponents = 16;
```

### Files Changed

- `src/core/constants.h` — add source comments to all constants

## Testing

- **Unit tests:** Existing `test_tool_registry.cpp` and `test_serialization.cpp` cover the validation and serialization paths. Add a test case for ResourceId parse error wrapping.
- **Integration tests:** Existing tests should continue to pass unchanged.
- **No new test files needed.**

## Non-Goals

- No refactoring of large files (serialization.cpp, cli/main.cpp) — separate effort
- No new error codes needed — assert_pixel uses existing `InvalidParamsError`, ResourceId uses existing `InvalidResourceId`
- No MCP-layer validation for parameters already checked in core
- Diff row `nullptr` values (DrawDiffRow, PassDiffRow) are intentional and not changed
