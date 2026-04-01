# Phase 2 Implementation Plan: Shader Editing, Extended Export, CI Assertions

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 13 new MCP tools (shader hot-editing, mesh/snapshot export, resource usage, CI assertions) bringing total to 40.

**Architecture:** Extends existing core/mcp/cli 3-layer pattern. New core modules for shader_edit, mesh, snapshot, usage, assertions. New MCP tool registration files. stb_image/stb_image_write added as header-only deps for assert_image.

**Tech Stack:** C++17, RenderDoc Replay API, nlohmann/json, stb_image, Google Test

**Spec:** `docs/superpowers/specs/2026-04-01-phase2-shader-mesh-assert-design.md`

---

## File Map

### New Files

| File | Responsibility |
|------|---------------|
| `src/core/shader_edit.h` | Shader edit declarations, state tracking types |
| `src/core/shader_edit.cpp` | Build/replace/restore implementation + cleanup |
| `src/core/mesh.h` | Mesh export declarations |
| `src/core/mesh.cpp` | GetPostVSData + buffer decode + OBJ/JSON generation |
| `src/core/snapshot.h` | Snapshot export declaration |
| `src/core/snapshot.cpp` | Aggregated draw state export |
| `src/core/usage.h` | Resource usage declaration |
| `src/core/usage.cpp` | GetUsage wrapper |
| `src/core/assertions.h` | Assertion result types + 5 function declarations |
| `src/core/assertions.cpp` | 5 assertion implementations |
| `src/mcp/tools/shader_edit_tools.cpp` | 5 shader edit tool registrations |
| `src/mcp/tools/mesh_tools.cpp` | export_mesh tool registration |
| `src/mcp/tools/snapshot_tools.cpp` | export_snapshot tool registration |
| `src/mcp/tools/usage_tools.cpp` | get_resource_usage tool registration |
| `src/mcp/tools/assertion_tools.cpp` | 5 assertion tool registrations |
| `third_party/stb_image.h` | PNG reading (header-only) |
| `third_party/stb_image_write.h` | PNG writing (header-only) |
| `tests/integration/test_tools_phase2.cpp` | Integration tests for all 13 new tools |

### Modified Files

| File | Changes |
|------|---------|
| `src/core/types.h` | New types: ShaderEncoding, ShaderBuildResult, MeshStage, MeshTopology, MeshVertex, MeshData, SnapshotResult, ResourceUsageEntry, ResourceUsageResult, AssertResult, ImageCompareResult |
| `src/core/errors.h` | New error codes: BuildFailed, UnknownShaderId, NoReplacementActive, UnknownEncoding, NoShaderBound, MeshNotAvailable, ImageSizeMismatch, ImageLoadFailed, InvalidPath |
| `src/core/session.h` | Add cleanupShaderEdits() call site in closeCurrent() |
| `src/core/session.cpp` | Call shader_edit cleanup before controller shutdown |
| `src/mcp/serialization.h` | New to_json declarations for Phase 2 types |
| `src/mcp/serialization.cpp` | New to_json implementations |
| `src/mcp/tools/tools.h` | 5 new register*Tools declarations |
| `src/mcp/mcp_server_default.cpp` | 5 new register*Tools calls |
| `src/cli/main.cpp` | 13 new CLI commands |
| `CMakeLists.txt` | New source files in renderdoc-core and renderdoc-mcp-lib targets |
| `tests/integration/test_tools_phase1.cpp` | Update SetUpTestSuite to register Phase 2 tools |
| `tests/unit/test_serialization.cpp` | New serialization unit tests |

---

## Task 1: Types and Error Codes

**Files:**
- Modify: `src/core/types.h:338` (append before closing namespace brace)
- Modify: `src/core/errors.h:21` (add new error codes)

- [ ] **Step 1: Add Phase 2 types to types.h**

Append before `} // namespace renderdoc::core` at line 338:

```cpp
// --- Shader Editing ---
enum class ShaderEncoding {
    Unknown = 0, DXBC = 1, GLSL = 2, SPIRV = 3,
    SPIRVAsm = 4, HLSL = 5, DXIL = 6,
    OpenGLSPIRV = 7, OpenGLSPIRVAsm = 8, Slang = 9
};

struct ShaderBuildResult {
    uint64_t shaderId = 0;   // 0 = failure
    std::string warnings;     // compiler warnings or error message
};

// --- Mesh Export ---
enum class MeshStage { VSOut = 1, GSOut = 2 };
enum class MeshTopology { TriangleList, TriangleStrip, TriangleFan, Other };

struct MeshVertex {
    float x = 0, y = 0, z = 0;
};

struct MeshData {
    uint32_t eventId = 0;
    MeshStage stage = MeshStage::VSOut;
    MeshTopology topology = MeshTopology::Other;
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<std::array<uint32_t, 3>> faces;
};

// --- Snapshot ---
struct SnapshotResult {
    std::string manifestPath;
    std::vector<std::string> files;
    std::vector<std::string> errors;
};

// --- Resource Usage ---
struct ResourceUsageEntry {
    uint32_t eventId = 0;
    std::string usage;
};

struct ResourceUsageResult {
    ResourceId resourceId = 0;
    std::vector<ResourceUsageEntry> entries;
};

// --- Assertions ---
struct AssertResult {
    bool pass = false;
    std::string message;
    nlohmann::json details;  // tool-specific fields
};

struct ImageCompareResult {
    bool pass = false;
    int diffPixels = 0;
    int totalPixels = 0;
    double diffRatio = 0.0;
    std::string diffOutputPath;
    std::string message;
};
```

**Required:** Add `#include <nlohmann/json.hpp>` at the top of types.h (after existing includes) for the `nlohmann::json` member in AssertResult. This is safe because nlohmann/json is already a public dependency of renderdoc-mcp-proto, which renderdoc-core links against.

- [ ] **Step 2: Add new error codes to errors.h**

In `src/core/errors.h`, add after `TargetNotFound` (line 21):

```cpp
        BuildFailed,
        UnknownShaderId,
        NoReplacementActive,
        UnknownEncoding,
        NoShaderBound,
        MeshNotAvailable,
        ImageSizeMismatch,
        ImageLoadFailed,
        InvalidPath
```

- [ ] **Step 3: Verify compilation**

Run: `cmake --build build --config Release --target renderdoc-mcp-proto 2>&1 | head -20`

Expected: Clean build (renderdoc-mcp-proto only needs nlohmann/json, no RenderDoc).

- [ ] **Step 4: Commit**

```bash
git add src/core/types.h src/core/errors.h
git commit -m "feat: add Phase 2 types and error codes (shader edit, mesh, assertions)"
```

---

## Task 2: stb_image Dependencies

**Files:**
- Create: `third_party/stb_image.h`
- Create: `third_party/stb_image_write.h`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Download stb headers**

```bash
mkdir -p third_party
curl -L -o third_party/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
curl -L -o third_party/stb_image_write.h https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
```

- [ ] **Step 2: Add include path to CMakeLists.txt**

In `CMakeLists.txt`, after `target_include_directories(renderdoc-core PUBLIC` block (line 42-46), add:

```cmake
    target_include_directories(renderdoc-core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        ${RENDERDOC_DIR}/renderdoc/api/replay
        ${RENDERDOC_DIR}/renderdoc/api
    )
```

(Just add `${CMAKE_CURRENT_SOURCE_DIR}/third_party` to the existing include list.)

- [ ] **Step 3: Commit**

```bash
git add third_party/stb_image.h third_party/stb_image_write.h CMakeLists.txt
git commit -m "deps: add stb_image and stb_image_write for PNG read/write"
```

---

## Task 3: Shader Edit Core

**Files:**
- Create: `src/core/shader_edit.h`
- Create: `src/core/shader_edit.cpp`
- Modify: `src/core/session.h`
- Modify: `src/core/session.cpp`
- Modify: `CMakeLists.txt:27-41` (add to renderdoc-core sources)

- [ ] **Step 1: Create shader_edit.h**

```cpp
#pragma once

#include "core/types.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace renderdoc::core {

class Session;

// State tracking for shader edits (lives in Session's lifetime)
struct ShaderEditState {
    // shader_id (uint64_t from ResourceId) -> RenderDoc ResourceId raw value
    std::map<uint64_t, uint64_t> builtShaders;
    // original_rid -> replacement_rid
    std::map<uint64_t, uint64_t> shaderReplacements;
};

// Query supported shader compilation encodings for current capture.
std::vector<std::string> getShaderEncodings(const Session& session);

// Compile shader source. Returns shader_id (0 on failure).
ShaderBuildResult buildShader(Session& session,
                              const std::string& source,
                              ShaderStage stage,
                              const std::string& entry,
                              const std::string& encoding);

// Replace shader at eventId/stage with a previously built shader.
// Returns the original shader's resource ID.
uint64_t replaceShader(Session& session,
                       uint32_t eventId,
                       ShaderStage stage,
                       uint64_t shaderId);

// Restore single shader at eventId/stage to original.
void restoreShader(Session& session,
                   uint32_t eventId,
                   ShaderStage stage);

// Restore all replacements and free all built shaders.
// Returns {restored_count, freed_count}.
std::pair<int, int> restoreAllShaders(Session& session);

// Cleanup function called by Session::closeCurrent() before controller shutdown.
void cleanupShaderEdits(Session& session);

} // namespace renderdoc::core
```

- [ ] **Step 2: Create shader_edit.cpp**

```cpp
#include "core/shader_edit.h"
#include "core/errors.h"
#include "core/session.h"

#include <renderdoc_replay.h>

namespace renderdoc::core {

// Module-level state (one per process, since we have one Session)
static ShaderEditState s_editState;

// Encoding name <-> RenderDoc enum value mapping
static const std::pair<std::string, int> s_encodingTable[] = {
    {"Unknown", 0}, {"DXBC", 1}, {"GLSL", 2}, {"SPIRV", 3},
    {"SPIRVAsm", 4}, {"HLSL", 5}, {"DXIL", 6},
    {"OpenGLSPIRV", 7}, {"OpenGLSPIRVAsm", 8}, {"Slang", 9}
};

static int encodingNameToValue(const std::string& name) {
    for (const auto& [n, v] : s_encodingTable) {
        // Case-insensitive comparison
        std::string lower = name;
        std::string lowerN = n;
        for (auto& c : lower) c = static_cast<char>(std::tolower(c));
        for (auto& c : lowerN) c = static_cast<char>(std::tolower(c));
        if (lower == lowerN) return v;
    }
    return -1;
}

static std::string encodingValueToName(int val) {
    for (const auto& [n, v] : s_encodingTable) {
        if (v == val) return n;
    }
    return "Unknown";
}

std::vector<std::string> getShaderEncodings(const Session& session) {
    auto* ctrl = session.controller();
    rdcarray<ShaderEncoding> encodings = ctrl->GetTargetShaderEncodings();
    std::vector<std::string> result;
    for (const auto& enc : encodings) {
        result.push_back(encodingValueToName(static_cast<int>(enc)));
    }
    return result;
}

ShaderBuildResult buildShader(Session& session,
                              const std::string& source,
                              ShaderStage stage,
                              const std::string& entry,
                              const std::string& encoding) {
    auto* ctrl = session.controller();

    int encVal = encodingNameToValue(encoding);
    if (encVal < 0)
        throw CoreError(CoreError::Code::UnknownEncoding,
                        "Unknown shader encoding: " + encoding);

    rdcstr sourceRdc(source.c_str());
    bytebuf sourceBytes(sourceRdc.size());
    memcpy(sourceBytes.data(), sourceRdc.c_str(), sourceRdc.size());

    ShaderCompileFlags flags;

    // Map ShaderStage enum to RenderDoc's ShaderStage
    static const ::ShaderStage stageMap[] = {
        ::ShaderStage::Vertex, ::ShaderStage::Hull, ::ShaderStage::Domain,
        ::ShaderStage::Geometry, ::ShaderStage::Pixel, ::ShaderStage::Compute
    };
    ::ShaderStage rdcStage = stageMap[static_cast<int>(stage)];

    ResourceId rid;
    rdcstr warnings;
    ctrl->BuildTargetShader(rdcstr(entry.c_str()),
                            static_cast<::ShaderEncoding>(encVal),
                            sourceBytes, flags, rdcStage, &rid, &warnings);

    uint64_t id = IsResourceValid(rid) ? GetResourceID(rid) : 0;
    if (id == 0) {
        throw CoreError(CoreError::Code::BuildFailed,
                        warnings.empty() ? "Shader build failed" : std::string(warnings.c_str()));
    }

    s_editState.builtShaders[id] = id;

    ShaderBuildResult result;
    result.shaderId = id;
    result.warnings = std::string(warnings.c_str());
    return result;
}

// Helper: get the shader ResourceId bound at (eventId, stage)
static uint64_t getOriginalShaderAtStage(IReplayController* ctrl,
                                          uint32_t eventId,
                                          ShaderStage stage) {
    ctrl->SetFrameEvent(eventId, true);
    const PipeState& pipe = ctrl->GetPipelineState();

    static const ::ShaderStage stageMap[] = {
        ::ShaderStage::Vertex, ::ShaderStage::Hull, ::ShaderStage::Domain,
        ::ShaderStage::Geometry, ::ShaderStage::Pixel, ::ShaderStage::Compute
    };
    ::ShaderStage rdcStage = stageMap[static_cast<int>(stage)];

    ResourceId rid = pipe.GetShader(rdcStage);
    uint64_t id = IsResourceValid(rid) ? GetResourceID(rid) : 0;
    if (id == 0)
        throw CoreError(CoreError::Code::NoShaderBound,
                        "No shader bound at specified stage");
    return id;
}

uint64_t replaceShader(Session& session,
                       uint32_t eventId,
                       ShaderStage stage,
                       uint64_t shaderId) {
    auto* ctrl = session.controller();

    auto it = s_editState.builtShaders.find(shaderId);
    if (it == s_editState.builtShaders.end())
        throw CoreError(CoreError::Code::UnknownShaderId,
                        "Unknown shader ID: " + std::to_string(shaderId));

    uint64_t originalId = getOriginalShaderAtStage(ctrl, eventId, stage);

    ResourceId originalRid = MakeResourceId(originalId);
    ResourceId replacementRid = MakeResourceId(shaderId);
    ctrl->ReplaceResource(originalRid, replacementRid);

    s_editState.shaderReplacements[originalId] = shaderId;
    return originalId;
}

void restoreShader(Session& session,
                   uint32_t eventId,
                   ShaderStage stage) {
    auto* ctrl = session.controller();
    uint64_t originalId = getOriginalShaderAtStage(ctrl, eventId, stage);

    auto it = s_editState.shaderReplacements.find(originalId);
    if (it == s_editState.shaderReplacements.end())
        throw CoreError(CoreError::Code::NoReplacementActive,
                        "No active replacement for this shader");

    ResourceId originalRid = MakeResourceId(originalId);
    ctrl->RemoveReplacement(originalRid);
    s_editState.shaderReplacements.erase(it);
}

std::pair<int, int> restoreAllShaders(Session& session) {
    auto* ctrl = session.controller();

    int restored = static_cast<int>(s_editState.shaderReplacements.size());
    int freed = static_cast<int>(s_editState.builtShaders.size());

    for (auto& [origId, replId] : s_editState.shaderReplacements) {
        ResourceId rid = MakeResourceId(origId);
        ctrl->RemoveReplacement(rid);
    }
    for (auto& [id, _] : s_editState.builtShaders) {
        ResourceId rid = MakeResourceId(id);
        ctrl->FreeTargetResource(rid);
    }

    s_editState.shaderReplacements.clear();
    s_editState.builtShaders.clear();
    return {restored, freed};
}

void cleanupShaderEdits(Session& session) {
    if (s_editState.builtShaders.empty() && s_editState.shaderReplacements.empty())
        return;
    // Controller may be null if session never fully opened
    try {
        auto* ctrl = session.controller();
        for (auto& [origId, replId] : s_editState.shaderReplacements) {
            ResourceId rid = MakeResourceId(origId);
            ctrl->RemoveReplacement(rid);
        }
        for (auto& [id, _] : s_editState.builtShaders) {
            ResourceId rid = MakeResourceId(id);
            ctrl->FreeTargetResource(rid);
        }
    } catch (...) {
        // Best-effort cleanup; controller may already be gone
    }
    s_editState.shaderReplacements.clear();
    s_editState.builtShaders.clear();
}

} // namespace renderdoc::core
```

**Important:** The exact RenderDoc API for `BuildTargetShader`, `IsResourceValid`, `GetResourceID`, `MakeResourceId` may differ by version. Verify against `renderdoc_replay.h` in the RenderDoc source. The rdc-cli handler (`handlers/shader_edit.py`) uses `int(rid) == 0` to test for failure, so `GetResourceID` returning 0 means invalid. Adjust the C++ wrappers accordingly based on what the RenderDoc C++ API actually provides.

- [ ] **Step 3: Wire cleanup into Session::closeCurrent()**

In `src/core/session.h`, add forward declaration at top:

```cpp
void cleanupShaderEdits(Session& session);
```

In `src/core/session.cpp`, add `#include "core/shader_edit.h"` at top. Then modify `closeCurrent()` (line 57).

Note: `Session::close()` (line 72) delegates to `closeCurrent()`, so adding cleanup here covers both the explicit close path and the re-open path (`Session::open()` calls `closeCurrent()` at line 78). No separate modification to `close()` is needed.

```cpp
void Session::closeCurrent() {
    // Clean up shader edits BEFORE shutting down controller
    if (m_controller) {
        cleanupShaderEdits(*this);
        m_controller->Shutdown();
        m_controller = nullptr;
    }
    if (m_captureFile) {
        m_captureFile->Shutdown();
        m_captureFile = nullptr;
    }
    m_currentEventId = 0;
    m_capturePath.clear();
    m_totalEvents = 0;
    m_api = GraphicsApi::Unknown;
}
```

- [ ] **Step 4: Add shader_edit.cpp to CMakeLists.txt**

In `CMakeLists.txt`, add `src/core/shader_edit.cpp` to the `renderdoc-core` target sources (after line 40):

```cmake
        src/core/texstats.cpp
        src/core/shader_edit.cpp
```

- [ ] **Step 5: Verify compilation**

Run: `cmake --build build --config Release --target renderdoc-core 2>&1 | tail -5`

Expected: Clean build.

- [ ] **Step 6: Commit**

```bash
git add src/core/shader_edit.h src/core/shader_edit.cpp src/core/session.h src/core/session.cpp CMakeLists.txt
git commit -m "feat(core): implement shader build/replace/restore with session cleanup"
```

---

## Task 4: Mesh Export Core

**Files:**
- Create: `src/core/mesh.h`
- Create: `src/core/mesh.cpp`
- Modify: `CMakeLists.txt:27-41`

- [ ] **Step 1: Create mesh.h**

```cpp
#pragma once

#include "core/types.h"
#include <optional>
#include <string>

namespace renderdoc::core {

class Session;

MeshData exportMesh(const Session& session,
                    uint32_t eventId,
                    MeshStage stage = MeshStage::VSOut);

// Format mesh data as Wavefront OBJ string.
std::string meshToObj(const MeshData& data);

} // namespace renderdoc::core
```

- [ ] **Step 2: Create mesh.cpp**

```cpp
#include "core/mesh.h"
#include "core/errors.h"
#include "core/session.h"

#include <renderdoc_replay.h>
#include <cmath>
#include <cstring>
#include <sstream>

namespace renderdoc::core {

// Decode a float component from raw bytes based on byte width.
static float decodeComponent(const uint8_t* data, uint32_t compByteWidth) {
    if (compByteWidth == 4) {
        float val;
        memcpy(&val, data, 4);
        return val;
    } else if (compByteWidth == 2) {
        // Half-float: use a simple conversion
        uint16_t h;
        memcpy(&h, data, 2);
        // IEEE 754 half -> float conversion
        uint32_t sign = (h >> 15) & 1;
        uint32_t exp = (h >> 10) & 0x1F;
        uint32_t mant = h & 0x3FF;
        uint32_t f;
        if (exp == 0) {
            if (mant == 0) f = sign << 31;
            else {
                exp = 1;
                while (!(mant & 0x400)) { mant <<= 1; exp--; }
                mant &= 0x3FF;
                f = (sign << 31) | ((exp + 127 - 15) << 23) | (mant << 13);
            }
        } else if (exp == 31) {
            f = (sign << 31) | 0x7F800000 | (mant << 13);
        } else {
            f = (sign << 31) | ((exp + 127 - 15) << 23) | (mant << 13);
        }
        float result;
        memcpy(&result, &f, 4);
        return result;
    } else if (compByteWidth == 1) {
        return static_cast<float>(data[0]) / 255.0f;
    }
    return 0.0f;
}

static MeshTopology convertTopology(Topology topo) {
    switch (topo) {
    case Topology::TriangleList:  return MeshTopology::TriangleList;
    case Topology::TriangleStrip: return MeshTopology::TriangleStrip;
    case Topology::TriangleFan:   return MeshTopology::TriangleFan;
    default:                      return MeshTopology::Other;
    }
}

static void generateFaces(MeshData& data) {
    data.faces.clear();
    if (data.indices.empty()) return;

    switch (data.topology) {
    case MeshTopology::TriangleList:
        for (size_t i = 0; i + 2 < data.indices.size(); i += 3) {
            data.faces.push_back({data.indices[i], data.indices[i+1], data.indices[i+2]});
        }
        break;
    case MeshTopology::TriangleStrip:
        for (size_t i = 0; i + 2 < data.indices.size(); ++i) {
            if (i % 2 == 0)
                data.faces.push_back({data.indices[i], data.indices[i+1], data.indices[i+2]});
            else
                data.faces.push_back({data.indices[i+1], data.indices[i], data.indices[i+2]});
        }
        break;
    case MeshTopology::TriangleFan:
        for (size_t i = 1; i + 1 < data.indices.size(); ++i) {
            data.faces.push_back({data.indices[0], data.indices[i], data.indices[i+1]});
        }
        break;
    default:
        break;
    }
}

MeshData exportMesh(const Session& session,
                    uint32_t eventId,
                    MeshStage stage) {
    auto* ctrl = session.controller();
    ctrl->SetFrameEvent(eventId, true);

    MeshOutput meshOut = ctrl->GetPostVSData(0, 0, static_cast<MeshDataStage>(stage));

    if (!IsResourceValid(meshOut.vertexResourceId))
        throw CoreError(CoreError::Code::MeshNotAvailable,
                        "No post-VS mesh data available for event " + std::to_string(eventId));

    MeshData result;
    result.eventId = eventId;
    result.stage = stage;
    result.topology = convertTopology(meshOut.topology);

    // Decode vertices
    uint32_t compCount = meshOut.format.compCount;
    uint32_t compByteWidth = meshOut.format.compByteWidth;
    uint32_t stride = meshOut.vertexByteStride;

    if (stride == 0 || compByteWidth == 0)
        throw CoreError(CoreError::Code::MeshNotAvailable, "Invalid mesh format");

    bytebuf vertexData = ctrl->GetBufferData(meshOut.vertexResourceId,
                                              meshOut.vertexByteOffset,
                                              meshOut.vertexByteSize);

    uint32_t vertexCount = (stride > 0) ? static_cast<uint32_t>(vertexData.size()) / stride : 0;
    result.vertices.reserve(vertexCount);

    for (uint32_t i = 0; i < vertexCount; ++i) {
        const uint8_t* base = vertexData.data() + i * stride;
        MeshVertex v;
        if (compCount >= 1) v.x = decodeComponent(base + 0 * compByteWidth, compByteWidth);
        if (compCount >= 2) v.y = decodeComponent(base + 1 * compByteWidth, compByteWidth);
        if (compCount >= 3) v.z = decodeComponent(base + 2 * compByteWidth, compByteWidth);
        result.vertices.push_back(v);
    }

    // Decode indices
    if (IsResourceValid(meshOut.indexResourceId) && meshOut.indexByteSize > 0) {
        bytebuf indexData = ctrl->GetBufferData(meshOut.indexResourceId,
                                                 meshOut.indexByteOffset,
                                                 meshOut.indexByteSize);
        uint32_t indexStride = meshOut.indexByteStride;
        uint32_t indexCount = (indexStride > 0) ? static_cast<uint32_t>(indexData.size()) / indexStride : 0;
        result.indices.reserve(indexCount);

        for (uint32_t i = 0; i < indexCount; ++i) {
            const uint8_t* base = indexData.data() + i * indexStride;
            uint32_t idx = 0;
            if (indexStride == 4) memcpy(&idx, base, 4);
            else if (indexStride == 2) { uint16_t s; memcpy(&s, base, 2); idx = s; }
            result.indices.push_back(idx);
        }
    } else {
        // No index buffer: generate sequential indices
        result.indices.resize(vertexCount);
        for (uint32_t i = 0; i < vertexCount; ++i) result.indices[i] = i;
    }

    generateFaces(result);
    return result;
}

std::string meshToObj(const MeshData& data) {
    std::ostringstream out;
    out << "# rdc mesh export: eid=" << data.eventId
        << " stage=" << (data.stage == MeshStage::VSOut ? "vs-out" : "gs-out")
        << " vertices=" << data.vertices.size()
        << " faces=" << data.faces.size() << "\n";

    out << std::fixed;
    out.precision(6);
    for (const auto& v : data.vertices) {
        out << "v " << v.x << " " << v.y << " " << v.z << "\n";
    }
    for (const auto& f : data.faces) {
        // OBJ indices are 1-based
        out << "f " << (f[0]+1) << " " << (f[1]+1) << " " << (f[2]+1) << "\n";
    }
    return out.str();
}

} // namespace renderdoc::core
```

**Note:** The exact RenderDoc types (`MeshOutput`, `MeshDataStage`, `Topology`, `bytebuf`) must match the version at `RENDERDOC_DIR/renderdoc/api/replay/renderdoc_replay.h`. Verify field names. The rdc-cli uses `GetPostVSData(0, 0, stage_val)` with stage 1=VSOut, 2=GSOut.

- [ ] **Step 3: Add mesh.cpp to CMakeLists.txt**

Add `src/core/mesh.cpp` to renderdoc-core sources.

- [ ] **Step 4: Verify compilation**

Run: `cmake --build build --config Release --target renderdoc-core 2>&1 | tail -5`

- [ ] **Step 5: Commit**

```bash
git add src/core/mesh.h src/core/mesh.cpp CMakeLists.txt
git commit -m "feat(core): implement mesh export with OBJ/JSON output"
```

---

## Task 5: Snapshot and Usage Core

**Files:**
- Create: `src/core/snapshot.h`
- Create: `src/core/snapshot.cpp`
- Create: `src/core/usage.h`
- Create: `src/core/usage.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create snapshot.h**

```cpp
#pragma once

#include "core/types.h"
#include <string>

namespace renderdoc::core {

class Session;

SnapshotResult exportSnapshot(Session& session,
                              uint32_t eventId,
                              const std::string& outputDir);

} // namespace renderdoc::core
```

- [ ] **Step 2: Create snapshot.cpp**

```cpp
#include "core/snapshot.h"
#include "core/errors.h"
#include "core/export.h"
#include "core/pipeline.h"
#include "core/session.h"
#include "core/shaders.h"
#include "mcp/serialization.h"

#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace renderdoc::core {

static std::string isoTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

SnapshotResult exportSnapshot(Session& session,
                              uint32_t eventId,
                              const std::string& outputDir) {
    SnapshotResult result;
    fs::create_directories(outputDir);

    // 1. Pipeline state (fatal on failure)
    auto pipeState = getPipelineState(session, eventId);
    {
        std::string path = (fs::path(outputDir) / "pipeline.json").string();
        std::ofstream f(path);
        f << mcp::to_json(pipeState).dump(2);
        result.files.push_back("pipeline.json");
    }

    // 2. Shader disassemblies (skip on failure)
    ShaderStage stages[] = {ShaderStage::Vertex, ShaderStage::Hull, ShaderStage::Domain,
                            ShaderStage::Geometry, ShaderStage::Pixel, ShaderStage::Compute};
    const char* stageNames[] = {"vs", "hs", "ds", "gs", "ps", "cs"};

    for (int i = 0; i < 6; ++i) {
        try {
            auto disasm = getShaderDisassembly(session, stages[i], eventId);
            if (!disasm.disassembly.empty()) {
                std::string filename = std::string("shader_") + stageNames[i] + ".txt";
                std::string path = (fs::path(outputDir) / filename).string();
                std::ofstream f(path);
                f << disasm.disassembly;
                result.files.push_back(filename);
            }
        } catch (const CoreError&) {
            // Shader not bound at this stage — skip silently
        }
    }

    // 3. Color render targets (stop on first not-found)
    for (int rt = 0; rt < 8; ++rt) {
        try {
            auto exportRes = exportRenderTarget(session, rt, outputDir);
            std::string filename = "color" + std::to_string(rt) + ".png";
            // exportRenderTarget already writes to outputDir, rename if needed
            result.files.push_back(fs::path(exportRes.outputPath).filename().string());
        } catch (const CoreError&) {
            break;  // No more render targets
        }
    }

    // 4. Depth target (skip on failure)
    // Depth export uses RT index -1 convention or special handling
    // For now, try to export depthTarget if it exists in pipeline state
    if (pipeState.depthTarget.has_value()) {
        try {
            auto depthExport = exportTexture(session, pipeState.depthTarget->id, outputDir);
            std::string depthFilename = fs::path(depthExport.outputPath).filename().string();
            // Rename to depth.png for consistency
            fs::path src = fs::path(outputDir) / depthFilename;
            fs::path dst = fs::path(outputDir) / "depth.png";
            if (src != dst && fs::exists(src)) {
                fs::rename(src, dst);
                depthFilename = "depth.png";
            }
            result.files.push_back(depthFilename);
        } catch (const CoreError&) {
            result.errors.push_back("Failed to export depth target");
        }
    }

    // 5. Write manifest
    {
        nlohmann::json manifest;
        manifest["eventId"] = eventId;
        manifest["timestamp"] = isoTimestamp();
        manifest["files"] = result.files;
        std::string path = (fs::path(outputDir) / "manifest.json").string();
        std::ofstream f(path);
        f << manifest.dump(2);
        result.manifestPath = path;
    }

    return result;
}

} // namespace renderdoc::core
```

- [ ] **Step 3: Create usage.h**

```cpp
#pragma once

#include "core/types.h"

namespace renderdoc::core {

class Session;

ResourceUsageResult getResourceUsage(const Session& session, ResourceId resourceId);

} // namespace renderdoc::core
```

- [ ] **Step 4: Create usage.cpp**

```cpp
#include "core/usage.h"
#include "core/errors.h"
#include "core/session.h"

#include <renderdoc_replay.h>

namespace renderdoc::core {

static std::string usageToString(ResourceUsage usage) {
    switch (usage) {
    case ResourceUsage::VertexBuffer:   return "VertexBuffer";
    case ResourceUsage::IndexBuffer:    return "IndexBuffer";
    case ResourceUsage::VS_Constants:   return "VS_Constants";
    case ResourceUsage::HS_Constants:   return "HS_Constants";
    case ResourceUsage::DS_Constants:   return "DS_Constants";
    case ResourceUsage::GS_Constants:   return "GS_Constants";
    case ResourceUsage::PS_Constants:   return "PS_Constants";
    case ResourceUsage::CS_Constants:   return "CS_Constants";
    case ResourceUsage::VS_Resource:    return "VS_Resource";
    case ResourceUsage::HS_Resource:    return "HS_Resource";
    case ResourceUsage::DS_Resource:    return "DS_Resource";
    case ResourceUsage::GS_Resource:    return "GS_Resource";
    case ResourceUsage::PS_Resource:    return "PS_Resource";
    case ResourceUsage::CS_Resource:    return "CS_Resource";
    case ResourceUsage::VS_RWResource:  return "VS_RWResource";
    case ResourceUsage::PS_RWResource:  return "PS_RWResource";
    case ResourceUsage::CS_RWResource:  return "CS_RWResource";
    case ResourceUsage::InputTarget:    return "InputTarget";
    case ResourceUsage::ColorTarget:    return "ColorTarget";
    case ResourceUsage::DepthStencilTarget: return "DepthStencilTarget";
    case ResourceUsage::Copy:           return "Copy";
    case ResourceUsage::CopySrc:        return "CopySrc";
    case ResourceUsage::CopyDst:        return "CopyDst";
    case ResourceUsage::Resolve:        return "Resolve";
    case ResourceUsage::ResolveSrc:     return "ResolveSrc";
    case ResourceUsage::ResolveDst:     return "ResolveDst";
    case ResourceUsage::Clear:          return "Clear";
    default:                            return "Unknown";
    }
}

ResourceUsageResult getResourceUsage(const Session& session, ResourceId resourceId) {
    auto* ctrl = session.controller();

    // Build ResourceId from our uint64_t
    ::ResourceId rid;
    // RenderDoc ResourceId construction varies by version
    // Using the same pattern as other core modules
    rid = MakeResourceId(resourceId);

    rdcarray<EventUsage> usages = ctrl->GetUsage(rid);

    ResourceUsageResult result;
    result.resourceId = resourceId;
    result.entries.reserve(usages.size());

    for (const auto& u : usages) {
        ResourceUsageEntry entry;
        entry.eventId = u.eventId;
        entry.usage = usageToString(u.usage);
        result.entries.push_back(entry);
    }
    return result;
}

} // namespace renderdoc::core
```

**Note:** The exact `ResourceUsage` enum values must be verified against `renderdoc_replay.h`. The rdc-cli Python code uses the Python enum names, which may differ slightly from C++ names. Check the actual header.

- [ ] **Step 5: Add to CMakeLists.txt**

Add `src/core/snapshot.cpp` and `src/core/usage.cpp` to renderdoc-core sources.

- [ ] **Step 6: Verify compilation**

Run: `cmake --build build --config Release --target renderdoc-core 2>&1 | tail -5`

- [ ] **Step 7: Commit**

```bash
git add src/core/snapshot.h src/core/snapshot.cpp src/core/usage.h src/core/usage.cpp CMakeLists.txt
git commit -m "feat(core): implement snapshot export and resource usage tracking"
```

---

## Task 6: Assertions Core

**Files:**
- Create: `src/core/assertions.h`
- Create: `src/core/assertions.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create assertions.h**

```cpp
#pragma once

#include "core/types.h"
#include <optional>
#include <string>
#include <vector>

namespace renderdoc::core {

class Session;

// assert_pixel: validate pixel RGBA value
AssertResult assertPixel(const Session& session,
                         uint32_t eventId,
                         uint32_t x, uint32_t y,
                         const std::vector<float>& expected,
                         float tolerance = 0.01f,
                         uint32_t target = 0);

// assert_state: validate pipeline state field
AssertResult assertState(const Session& session,
                         uint32_t eventId,
                         const std::string& path,
                         const std::string& expected);

// assert_image: compare two PNG files pixel-by-pixel
ImageCompareResult assertImage(const std::string& expectedPath,
                               const std::string& actualPath,
                               double threshold = 0.0,
                               const std::string& diffOutputPath = "");

// assert_count: validate resource/event/draw counts
AssertResult assertCount(const Session& session,
                         const std::string& what,
                         int expected,
                         const std::string& op = "eq");

// assert_clean: validate no debug messages above severity
AssertResult assertClean(const Session& session,
                         const std::string& minSeverity = "high");

} // namespace renderdoc::core
```

- [ ] **Step 2: Create assertions.cpp**

```cpp
#include "core/assertions.h"
#include "core/errors.h"
#include "core/events.h"
#include "core/info.h"
#include "core/pipeline.h"
#include "core/pixel.h"
#include "core/resources.h"
#include "core/session.h"
#include "mcp/serialization.h"

#include <cmath>
#include <sstream>

// stb_image for PNG loading
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// stb_image_write for diff output
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <renderdoc_replay.h>

namespace renderdoc::core {

// --- assert_pixel ---

AssertResult assertPixel(const Session& session,
                         uint32_t eventId,
                         uint32_t x, uint32_t y,
                         const std::vector<float>& expected,
                         float tolerance,
                         uint32_t target) {
    if (expected.size() != 4)
        throw CoreError(CoreError::Code::InternalError, "expected must have 4 components (RGBA)");

    auto pixel = pickPixel(session, x, y, target, eventId);

    AssertResult result;
    result.details["actual"] = {pixel.color.floatValue[0], pixel.color.floatValue[1],
                                pixel.color.floatValue[2], pixel.color.floatValue[3]};
    result.details["expected"] = expected;
    result.details["tolerance"] = tolerance;

    bool allMatch = true;
    for (int i = 0; i < 4; ++i) {
        if (std::fabs(pixel.color.floatValue[i] - expected[i]) > tolerance) {
            allMatch = false;
            break;
        }
    }

    result.pass = allMatch;
    if (allMatch) {
        result.message = "pixel (" + std::to_string(x) + ", " + std::to_string(y) +
                         ") matches expected value within tolerance";
    } else {
        std::ostringstream ss;
        ss << "pixel (" << x << ", " << y << ") expected ["
           << expected[0] << ", " << expected[1] << ", "
           << expected[2] << ", " << expected[3] << "], got ["
           << pixel.color.floatValue[0] << ", " << pixel.color.floatValue[1] << ", "
           << pixel.color.floatValue[2] << ", " << pixel.color.floatValue[3] << "]";
        result.message = ss.str();
    }
    return result;
}

// --- assert_state ---

// Navigate a JSON object by dot-separated path with [N] array index support.
static nlohmann::json navigatePath(const nlohmann::json& root, const std::string& path) {
    nlohmann::json current = root;
    std::string segment;
    std::istringstream stream(path);

    while (std::getline(stream, segment, '.')) {
        // Check for array index: "field[N]"
        auto bracket = segment.find('[');
        if (bracket != std::string::npos) {
            std::string field = segment.substr(0, bracket);
            auto closeBracket = segment.find(']', bracket);
            if (closeBracket == std::string::npos)
                throw CoreError(CoreError::Code::InvalidPath, "Unclosed bracket in path: " + path);
            int index = std::stoi(segment.substr(bracket + 1, closeBracket - bracket - 1));

            if (!field.empty()) {
                if (!current.contains(field))
                    throw CoreError(CoreError::Code::InvalidPath, "Field not found: " + field);
                current = current[field];
            }
            if (!current.is_array() || index < 0 || index >= static_cast<int>(current.size()))
                throw CoreError(CoreError::Code::InvalidPath,
                                "Invalid array index: " + std::to_string(index));
            current = current[index];
        } else {
            if (!current.contains(segment))
                throw CoreError(CoreError::Code::InvalidPath, "Field not found: " + segment);
            current = current[segment];
        }
    }
    return current;
}

AssertResult assertState(const Session& session,
                         uint32_t eventId,
                         const std::string& path,
                         const std::string& expected) {
    auto pipeState = getPipelineState(session, eventId);
    nlohmann::json pipeJson = mcp::to_json(pipeState);

    nlohmann::json value = navigatePath(pipeJson, path);

    // Normalize to string for comparison
    std::string actual;
    if (value.is_boolean()) actual = value.get<bool>() ? "true" : "false";
    else if (value.is_number_integer()) actual = std::to_string(value.get<int64_t>());
    else if (value.is_number_float()) actual = std::to_string(value.get<double>());
    else if (value.is_string()) actual = value.get<std::string>();
    else actual = value.dump();

    AssertResult result;
    result.pass = (actual == expected);
    result.details["actual"] = actual;
    result.details["expected"] = expected;
    result.details["path"] = path;

    if (result.pass) {
        result.message = path + " = " + actual;
    } else {
        result.message = path + " = " + actual + " (expected " + expected + ")";
    }
    return result;
}

// --- assert_image ---

ImageCompareResult assertImage(const std::string& expectedPath,
                               const std::string& actualPath,
                               double threshold,
                               const std::string& diffOutputPath) {
    int ew, eh, ec, aw, ah, ac;
    unsigned char* expectedData = stbi_load(expectedPath.c_str(), &ew, &eh, &ec, 4);
    if (!expectedData)
        throw CoreError(CoreError::Code::ImageLoadFailed,
                        "Failed to load expected image: " + expectedPath);

    unsigned char* actualData = stbi_load(actualPath.c_str(), &aw, &ah, &ac, 4);
    if (!actualData) {
        stbi_image_free(expectedData);
        throw CoreError(CoreError::Code::ImageLoadFailed,
                        "Failed to load actual image: " + actualPath);
    }

    if (ew != aw || eh != ah) {
        stbi_image_free(expectedData);
        stbi_image_free(actualData);
        throw CoreError(CoreError::Code::ImageSizeMismatch,
                        "Image size mismatch: " + std::to_string(ew) + "x" + std::to_string(eh) +
                        " vs " + std::to_string(aw) + "x" + std::to_string(ah));
    }

    int totalPixels = ew * eh;
    int diffPixels = 0;

    // Allocate diff image if output requested
    std::vector<unsigned char> diffImage;
    if (!diffOutputPath.empty()) {
        diffImage.resize(totalPixels * 4);
    }

    for (int i = 0; i < totalPixels; ++i) {
        int offset = i * 4;
        bool same = (expectedData[offset] == actualData[offset] &&
                     expectedData[offset+1] == actualData[offset+1] &&
                     expectedData[offset+2] == actualData[offset+2] &&
                     expectedData[offset+3] == actualData[offset+3]);
        if (!same) diffPixels++;

        if (!diffOutputPath.empty()) {
            if (!same) {
                // Red overlay for different pixels
                diffImage[offset]   = 255;
                diffImage[offset+1] = 0;
                diffImage[offset+2] = 0;
                diffImage[offset+3] = 255;
            } else {
                // Grayscale version of expected for unchanged pixels
                uint8_t gray = static_cast<uint8_t>(
                    0.299f * expectedData[offset] +
                    0.587f * expectedData[offset+1] +
                    0.114f * expectedData[offset+2]);
                diffImage[offset]   = gray;
                diffImage[offset+1] = gray;
                diffImage[offset+2] = gray;
                diffImage[offset+3] = expectedData[offset+3];
            }
        }
    }

    stbi_image_free(expectedData);
    stbi_image_free(actualData);

    // Write diff image
    if (!diffOutputPath.empty() && diffPixels > 0) {
        stbi_write_png(diffOutputPath.c_str(), ew, eh, 4, diffImage.data(), ew * 4);
    }

    double diffRatio = (totalPixels > 0) ? (static_cast<double>(diffPixels) / totalPixels * 100.0) : 0.0;

    ImageCompareResult result;
    result.pass = (diffRatio <= threshold);
    result.diffPixels = diffPixels;
    result.totalPixels = totalPixels;
    result.diffRatio = diffRatio;
    result.diffOutputPath = diffOutputPath;

    if (result.pass) {
        result.message = "images match (diff ratio: " + std::to_string(diffRatio) + "%)";
    } else {
        result.message = "images differ: " + std::to_string(diffPixels) + "/" +
                         std::to_string(totalPixels) + " pixels (" +
                         std::to_string(diffRatio) + "%)";
    }
    return result;
}

// --- assert_count ---

AssertResult assertCount(const Session& session,
                         const std::string& what,
                         int expected,
                         const std::string& op) {
    auto* ctrl = session.controller();
    int actual = 0;

    if (what == "events") {
        // Use total from session status (no limit)
        auto st = session.status();
        actual = static_cast<int>(st.totalEvents);
    } else if (what == "draws") {
        // Count all draws by walking action tree (no limit)
        auto draws = listDraws(session, "", UINT32_MAX);
        actual = static_cast<int>(draws.size());
    } else if (what == "textures") {
        auto textures = ctrl->GetTextures();
        actual = static_cast<int>(textures.size());
    } else if (what == "buffers") {
        auto buffers = ctrl->GetBuffers();
        actual = static_cast<int>(buffers.size());
    } else if (what == "passes") {
        auto passes = listPasses(session);
        actual = static_cast<int>(passes.size());
    } else {
        throw CoreError(CoreError::Code::InternalError,
                        "Unknown count target: " + what + ". Use: draws, events, textures, buffers, passes");
    }

    bool pass = false;
    if (op == "eq") pass = (actual == expected);
    else if (op == "gt") pass = (actual > expected);
    else if (op == "lt") pass = (actual < expected);
    else if (op == "ge") pass = (actual >= expected);
    else if (op == "le") pass = (actual <= expected);
    else throw CoreError(CoreError::Code::InternalError, "Unknown operator: " + op);

    AssertResult result;
    result.pass = pass;
    result.details["actual"] = actual;
    result.details["expected"] = expected;
    result.details["op"] = op;
    result.details["what"] = what;

    result.message = what + " = " + std::to_string(actual) +
                     " (expected " + op + " " + std::to_string(expected) + ")";
    return result;
}

// --- assert_clean ---

static int severityLevel(const std::string& sev) {
    if (sev == "high") return 3;
    if (sev == "medium") return 2;
    if (sev == "low") return 1;
    if (sev == "info") return 0;
    return -1;
}

AssertResult assertClean(const Session& session,
                         const std::string& minSeverity) {
    auto messages = getLog(session, minSeverity);

    AssertResult result;
    result.pass = messages.empty();
    result.details["count"] = static_cast<int>(messages.size());
    result.details["minSeverity"] = minSeverity;

    if (!messages.empty()) {
        auto arr = nlohmann::json::array();
        for (const auto& msg : messages) {
            arr.push_back({{"severity", msg.severity},
                           {"eventId", msg.eventId},
                           {"message", msg.message}});
        }
        result.details["messages"] = arr;
    }

    if (result.pass) {
        result.message = "no messages at severity >= " + minSeverity;
    } else {
        result.message = std::to_string(messages.size()) + " message(s) at severity >= " + minSeverity;
    }
    return result;
}

} // namespace renderdoc::core
```

**Important:** `STB_IMAGE_IMPLEMENTATION` and `STB_IMAGE_WRITE_IMPLEMENTATION` must appear in exactly one .cpp file. This file is the designated location.

- [ ] **Step 3: Add assertions.cpp to CMakeLists.txt**

Add `src/core/assertions.cpp` to renderdoc-core sources.

- [ ] **Step 4: Verify compilation**

Run: `cmake --build build --config Release --target renderdoc-core 2>&1 | tail -10`

- [ ] **Step 5: Commit**

```bash
git add src/core/assertions.h src/core/assertions.cpp CMakeLists.txt
git commit -m "feat(core): implement 5 CI assertion functions (pixel, state, image, count, clean)"
```

---

## Task 7: Serialization for Phase 2 Types

**Files:**
- Modify: `src/mcp/serialization.h`
- Modify: `src/mcp/serialization.cpp`

- [ ] **Step 1: Add declarations to serialization.h**

Add after `nlohmann::json to_json(const core::TextureStats& stats);` (line 52):

```cpp
// Phase 2 types
nlohmann::json to_json(const core::ShaderBuildResult& result);
nlohmann::json to_json(const core::MeshVertex& v);
nlohmann::json to_json(const core::MeshData& data);
nlohmann::json to_json(const core::SnapshotResult& result);
nlohmann::json to_json(const core::ResourceUsageEntry& entry);
nlohmann::json to_json(const core::ResourceUsageResult& result);
nlohmann::json to_json(const core::AssertResult& result);
nlohmann::json to_json(const core::ImageCompareResult& result);
```

- [ ] **Step 2: Add implementations to serialization.cpp**

Append at end of file (before closing namespace brace):

```cpp
// --- Phase 2 serialization ---

nlohmann::json to_json(const core::ShaderBuildResult& result) {
    return {{"shaderId", result.shaderId}, {"warnings", result.warnings}};
}

nlohmann::json to_json(const core::MeshVertex& v) {
    return {{"x", v.x}, {"y", v.y}, {"z", v.z}};
}

nlohmann::json to_json(const core::MeshData& data) {
    nlohmann::json j;
    j["eventId"] = data.eventId;
    j["stage"] = (data.stage == core::MeshStage::VSOut) ? "vs-out" : "gs-out";

    switch (data.topology) {
    case core::MeshTopology::TriangleList:  j["topology"] = "TriangleList"; break;
    case core::MeshTopology::TriangleStrip: j["topology"] = "TriangleStrip"; break;
    case core::MeshTopology::TriangleFan:   j["topology"] = "TriangleFan"; break;
    default:                                j["topology"] = "Other"; break;
    }

    j["vertexCount"] = data.vertices.size();
    j["faceCount"] = data.faces.size();
    j["vertices"] = to_json_array(data.vertices);

    auto indicesArr = nlohmann::json::array();
    for (auto idx : data.indices) indicesArr.push_back(idx);
    j["indices"] = indicesArr;

    auto facesArr = nlohmann::json::array();
    for (const auto& f : data.faces) facesArr.push_back({f[0], f[1], f[2]});
    j["faces"] = facesArr;

    return j;
}

nlohmann::json to_json(const core::SnapshotResult& result) {
    return {{"manifestPath", result.manifestPath},
            {"files", result.files},
            {"errors", result.errors}};
}

nlohmann::json to_json(const core::ResourceUsageEntry& entry) {
    return {{"eventId", entry.eventId}, {"usage", entry.usage}};
}

nlohmann::json to_json(const core::ResourceUsageResult& result) {
    return {{"resourceId", resourceIdToString(result.resourceId)},
            {"entries", to_json_array(result.entries)}};
}

nlohmann::json to_json(const core::AssertResult& result) {
    nlohmann::json j;
    j["pass"] = result.pass;
    j["message"] = result.message;
    // Merge details into top level
    for (auto& [key, val] : result.details.items()) {
        j[key] = val;
    }
    return j;
}

nlohmann::json to_json(const core::ImageCompareResult& result) {
    nlohmann::json j;
    j["pass"] = result.pass;
    j["diffPixels"] = result.diffPixels;
    j["totalPixels"] = result.totalPixels;
    j["diffRatio"] = result.diffRatio;
    j["message"] = result.message;
    if (!result.diffOutputPath.empty())
        j["diffOutputPath"] = result.diffOutputPath;
    return j;
}
```

- [ ] **Step 3: Verify compilation**

Run: `cmake --build build --config Release --target renderdoc-mcp-proto 2>&1 | tail -5`

- [ ] **Step 4: Commit**

```bash
git add src/mcp/serialization.h src/mcp/serialization.cpp
git commit -m "feat(mcp): add serialization for Phase 2 types"
```

---

## Task 8: MCP Tool Registrations (Shader Edit)

**Files:**
- Create: `src/mcp/tools/shader_edit_tools.cpp`
- Modify: `src/mcp/tools/tools.h`
- Modify: `src/mcp/mcp_server_default.cpp`
- Modify: `CMakeLists.txt:87-101`

- [ ] **Step 1: Create shader_edit_tools.cpp**

```cpp
#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/shader_edit.h"

namespace renderdoc::mcp::tools {

void registerShaderEditTools(ToolRegistry& registry) {

    registry.registerTool({
        "shader_encodings",
        "List shader compilation encodings supported by the current capture. "
        "Call this before shader_build to determine valid encoding values.",
        {{"type", "object"}, {"properties", nlohmann::json::object()},
         {"required", nlohmann::json::array()}},
        [](core::Session& session, const nlohmann::json&) -> nlohmann::json {
            auto encodings = core::getShaderEncodings(session);
            return {{"encodings", encodings}};
        }
    });

    registry.registerTool({
        "shader_build",
        "Compile shader source code. Returns a shaderId for use with shader_replace. "
        "Encoding must be one of the values from shader_encodings.",
        {{"type", "object"},
         {"properties", {
             {"source",   {{"type", "string"}, {"description", "Shader source code"}}},
             {"stage",    {{"type", "string"}, {"enum", {"vs","hs","ds","gs","ps","cs"}},
                           {"description", "Shader stage"}}},
             {"entry",    {{"type", "string"}, {"description", "Entry point name (default: main)"}}},
             {"encoding", {{"type", "string"}, {"description", "Shader encoding (from shader_encodings)"}}}
         }},
         {"required", nlohmann::json::array({"source", "stage", "encoding"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            auto source = args["source"].get<std::string>();
            auto stage = parseShaderStage(args["stage"].get<std::string>());
            auto entry = args.value("entry", std::string("main"));
            auto encoding = args["encoding"].get<std::string>();
            auto result = core::buildShader(session, source, stage, entry, encoding);
            return to_json(result);
        }
    });

    registry.registerTool({
        "shader_replace",
        "Replace a shader at the given event/stage with a previously built shader. "
        "Affects ALL draws using the same shader globally.",
        {{"type", "object"},
         {"properties", {
             {"eventId",  {{"type", "integer"}, {"description", "Event ID to locate shader"}}},
             {"stage",    {{"type", "string"}, {"enum", {"vs","hs","ds","gs","ps","cs"}},
                           {"description", "Shader stage"}}},
             {"shaderId", {{"type", "integer"}, {"description", "Built shader ID from shader_build"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "stage", "shaderId"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            uint32_t eventId = args["eventId"].get<uint32_t>();
            auto stage = parseShaderStage(args["stage"].get<std::string>());
            uint64_t shaderId = args["shaderId"].get<uint64_t>();
            uint64_t originalId = core::replaceShader(session, eventId, stage, shaderId);
            return {{"originalId", originalId},
                    {"message", "Replacement active. Affects all draws using this shader."}};
        }
    });

    registry.registerTool({
        "shader_restore",
        "Restore a single shader to its original version.",
        {{"type", "object"},
         {"properties", {
             {"eventId", {{"type", "integer"}, {"description", "Event ID"}}},
             {"stage",   {{"type", "string"}, {"enum", {"vs","hs","ds","gs","ps","cs"}},
                          {"description", "Shader stage"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "stage"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            uint32_t eventId = args["eventId"].get<uint32_t>();
            auto stage = parseShaderStage(args["stage"].get<std::string>());
            core::restoreShader(session, eventId, stage);
            return {{"restored", true}};
        }
    });

    registry.registerTool({
        "shader_restore_all",
        "Restore all replaced shaders and free all built shader resources.",
        {{"type", "object"}, {"properties", nlohmann::json::object()},
         {"required", nlohmann::json::array()}},
        [](core::Session& session, const nlohmann::json&) -> nlohmann::json {
            auto [restored, freed] = core::restoreAllShaders(session);
            return {{"restoredCount", restored}, {"freedCount", freed}};
        }
    });
}

} // namespace renderdoc::mcp::tools
```

- [ ] **Step 2: Add declaration to tools.h**

Add after `void registerTexStatsTools(ToolRegistry& registry);` (line 18):

```cpp
void registerShaderEditTools(ToolRegistry& registry);
```

- [ ] **Step 3: Add registration to mcp_server_default.cpp**

Add after `tools::registerTexStatsTools(*m_registry);` (line 25):

```cpp
    tools::registerShaderEditTools(*m_registry);
```

- [ ] **Step 4: Add to CMakeLists.txt**

Add `src/mcp/tools/shader_edit_tools.cpp` to renderdoc-mcp-lib sources.

- [ ] **Step 5: Verify compilation**

Run: `cmake --build build --config Release --target renderdoc-mcp-lib 2>&1 | tail -5`

- [ ] **Step 6: Commit**

```bash
git add src/mcp/tools/shader_edit_tools.cpp src/mcp/tools/tools.h src/mcp/mcp_server_default.cpp CMakeLists.txt
git commit -m "feat(mcp): register 5 shader edit tools"
```

---

## Task 9: MCP Tool Registrations (Mesh, Snapshot, Usage, Assertions)

**Files:**
- Create: `src/mcp/tools/mesh_tools.cpp`
- Create: `src/mcp/tools/snapshot_tools.cpp`
- Create: `src/mcp/tools/usage_tools.cpp`
- Create: `src/mcp/tools/assertion_tools.cpp`
- Modify: `src/mcp/tools/tools.h`
- Modify: `src/mcp/mcp_server_default.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create mesh_tools.cpp**

```cpp
#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/mesh.h"

namespace renderdoc::mcp::tools {

void registerMeshTools(ToolRegistry& registry) {

    registry.registerTool({
        "export_mesh",
        "Export post-transform vertex data from a draw call as OBJ or JSON. "
        "Decodes vertex positions and generates triangle faces from topology.",
        {{"type", "object"},
         {"properties", {
             {"eventId",    {{"type", "integer"}, {"description", "Draw call event ID"}}},
             {"stage",      {{"type", "string"}, {"enum", {"vs-out", "gs-out"}},
                             {"description", "Post-transform stage (default: vs-out)"}}},
             {"format",     {{"type", "string"}, {"enum", {"obj", "json"}},
                             {"description", "Output format (default: obj)"}}},
             {"outputPath", {{"type", "string"}, {"description", "File path to write (optional, returns inline if omitted)"}}}
         }},
         {"required", nlohmann::json::array({"eventId"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            uint32_t eventId = args["eventId"].get<uint32_t>();
            std::string stageStr = args.value("stage", std::string("vs-out"));
            core::MeshStage stage = (stageStr == "gs-out") ? core::MeshStage::GSOut : core::MeshStage::VSOut;
            std::string format = args.value("format", std::string("obj"));

            auto data = core::exportMesh(session, eventId, stage);

            if (args.contains("outputPath")) {
                std::string path = args["outputPath"].get<std::string>();
                std::ofstream f(path);
                if (format == "json") f << to_json(data).dump(2);
                else f << core::meshToObj(data);
                return {{"outputPath", path}, {"vertexCount", data.vertices.size()},
                        {"faceCount", data.faces.size()}};
            }

            if (format == "json") return to_json(data);
            return {{"obj", core::meshToObj(data)}, {"vertexCount", data.vertices.size()},
                    {"faceCount", data.faces.size()}};
        }
    });
}

} // namespace renderdoc::mcp::tools
```

- [ ] **Step 2: Create snapshot_tools.cpp**

```cpp
#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/snapshot.h"

namespace renderdoc::mcp::tools {

void registerSnapshotTools(ToolRegistry& registry) {

    registry.registerTool({
        "export_snapshot",
        "Export complete draw call state: pipeline, shaders, render targets, and depth. "
        "Creates a directory with manifest.json indexing all exported files.",
        {{"type", "object"},
         {"properties", {
             {"eventId",   {{"type", "integer"}, {"description", "Draw call event ID"}}},
             {"outputDir", {{"type", "string"}, {"description", "Output directory path"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "outputDir"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            uint32_t eventId = args["eventId"].get<uint32_t>();
            auto outputDir = args["outputDir"].get<std::string>();
            auto result = core::exportSnapshot(session, eventId, outputDir);
            return to_json(result);
        }
    });
}

} // namespace renderdoc::mcp::tools
```

- [ ] **Step 3: Create usage_tools.cpp**

```cpp
#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/usage.h"

namespace renderdoc::mcp::tools {

void registerUsageTools(ToolRegistry& registry) {

    registry.registerTool({
        "get_resource_usage",
        "Get the usage history of a resource across all events. "
        "Shows which events read, write, or bind the resource.",
        {{"type", "object"},
         {"properties", {
             {"resourceId", {{"type", "string"}, {"description", "Resource ID (e.g. ResourceId::123)"}}}
         }},
         {"required", nlohmann::json::array({"resourceId"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            auto rid = parseResourceId(args["resourceId"].get<std::string>());
            auto result = core::getResourceUsage(session, rid);
            return to_json(result);
        }
    });
}

} // namespace renderdoc::mcp::tools
```

- [ ] **Step 4: Create assertion_tools.cpp**

```cpp
#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/assertions.h"

namespace renderdoc::mcp::tools {

void registerAssertionTools(ToolRegistry& registry) {

    registry.registerTool({
        "assert_pixel",
        "Assert that a pixel at (x,y) matches expected RGBA values within tolerance. "
        "Returns pass/fail with actual vs expected values.",
        {{"type", "object"},
         {"properties", {
             {"eventId",   {{"type", "integer"}, {"description", "Event ID"}}},
             {"x",         {{"type", "integer"}, {"description", "Pixel X coordinate"}}},
             {"y",         {{"type", "integer"}, {"description", "Pixel Y coordinate"}}},
             {"expected",  {{"type", "array"}, {"items", {{"type", "number"}}},
                            {"description", "[R, G, B, A] float values"}}},
             {"tolerance", {{"type", "number"}, {"description", "Per-channel tolerance (default 0.01)"}}},
             {"target",    {{"type", "integer"}, {"description", "Render target index (default 0)"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "x", "y", "expected"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            uint32_t eventId = args["eventId"].get<uint32_t>();
            uint32_t x = args["x"].get<uint32_t>();
            uint32_t y = args["y"].get<uint32_t>();
            auto expected = args["expected"].get<std::vector<float>>();
            float tolerance = args.value("tolerance", 0.01f);
            uint32_t target = args.value("target", 0u);
            auto result = core::assertPixel(session, eventId, x, y, expected, tolerance, target);
            return to_json(result);
        }
    });

    registry.registerTool({
        "assert_state",
        "Assert that a pipeline state field matches an expected value. "
        "Path uses dot notation matching the pipeline JSON output (e.g. vertexShader.entryPoint).",
        {{"type", "object"},
         {"properties", {
             {"eventId",  {{"type", "integer"}, {"description", "Event ID"}}},
             {"path",     {{"type", "string"}, {"description", "Dot-separated path (e.g. vertexShader.entryPoint)"}}},
             {"expected", {{"type", "string"}, {"description", "Expected value (string comparison)"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "path", "expected"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            uint32_t eventId = args["eventId"].get<uint32_t>();
            auto path = args["path"].get<std::string>();
            auto expected = args["expected"].get<std::string>();
            auto result = core::assertState(session, eventId, path, expected);
            return to_json(result);
        }
    });

    registry.registerTool({
        "assert_image",
        "Compare two PNG images pixel-by-pixel. Returns pass/fail with diff statistics. "
        "Optionally writes a diff visualization PNG.",
        {{"type", "object"},
         {"properties", {
             {"expectedPath",  {{"type", "string"}, {"description", "Path to expected PNG"}}},
             {"actualPath",    {{"type", "string"}, {"description", "Path to actual PNG"}}},
             {"threshold",     {{"type", "number"}, {"description", "Max diff ratio % to pass (default 0.0)"}}},
             {"diffOutputPath",{{"type", "string"}, {"description", "Path to write diff visualization PNG"}}}
         }},
         {"required", nlohmann::json::array({"expectedPath", "actualPath"})}},
        [](core::Session&, const nlohmann::json& args) -> nlohmann::json {
            auto expectedPath = args["expectedPath"].get<std::string>();
            auto actualPath = args["actualPath"].get<std::string>();
            double threshold = args.value("threshold", 0.0);
            auto diffOutput = args.value("diffOutputPath", std::string(""));
            auto result = core::assertImage(expectedPath, actualPath, threshold, diffOutput);
            return to_json(result);
        }
    });

    registry.registerTool({
        "assert_count",
        "Assert that a count of resources/events/draws matches expected value. "
        "Uses accurate counts bypassing default list limits.",
        {{"type", "object"},
         {"properties", {
             {"what",     {{"type", "string"}, {"enum", {"draws","events","textures","buffers","passes"}},
                           {"description", "What to count"}}},
             {"expected", {{"type", "integer"}, {"description", "Expected count"}}},
             {"op",       {{"type", "string"}, {"enum", {"eq","gt","lt","ge","le"}},
                           {"description", "Comparison operator (default: eq)"}}}
         }},
         {"required", nlohmann::json::array({"what", "expected"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            auto what = args["what"].get<std::string>();
            int expected = args["expected"].get<int>();
            auto op = args.value("op", std::string("eq"));
            auto result = core::assertCount(session, what, expected, op);
            return to_json(result);
        }
    });

    registry.registerTool({
        "assert_clean",
        "Assert that no debug/validation messages exist at or above the specified severity. "
        "Returns pass/fail with any matching messages.",
        {{"type", "object"},
         {"properties", {
             {"minSeverity", {{"type", "string"}, {"enum", {"high","medium","low","info"}},
                              {"description", "Minimum severity to fail on (default: high)"}}}
         }},
         {"required", nlohmann::json::array()}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            auto minSeverity = args.value("minSeverity", std::string("high"));
            auto result = core::assertClean(session, minSeverity);
            return to_json(result);
        }
    });
}

} // namespace renderdoc::mcp::tools
```

- [ ] **Step 5: Add all 4 declarations to tools.h**

Add after `void registerShaderEditTools(ToolRegistry& registry);`:

```cpp
void registerMeshTools(ToolRegistry& registry);
void registerSnapshotTools(ToolRegistry& registry);
void registerUsageTools(ToolRegistry& registry);
void registerAssertionTools(ToolRegistry& registry);
```

- [ ] **Step 6: Add all 4 registrations to mcp_server_default.cpp**

Add after `tools::registerShaderEditTools(*m_registry);`:

```cpp
    tools::registerMeshTools(*m_registry);
    tools::registerSnapshotTools(*m_registry);
    tools::registerUsageTools(*m_registry);
    tools::registerAssertionTools(*m_registry);
```

- [ ] **Step 7: Add all 4 source files to CMakeLists.txt**

Add to renderdoc-mcp-lib sources:

```cmake
        src/mcp/tools/shader_edit_tools.cpp
        src/mcp/tools/mesh_tools.cpp
        src/mcp/tools/snapshot_tools.cpp
        src/mcp/tools/usage_tools.cpp
        src/mcp/tools/assertion_tools.cpp
```

- [ ] **Step 8: Verify compilation**

Run: `cmake --build build --config Release --target renderdoc-mcp 2>&1 | tail -10`

Expected: Clean build of the full executable.

- [ ] **Step 9: Commit**

```bash
git add src/mcp/tools/mesh_tools.cpp src/mcp/tools/snapshot_tools.cpp src/mcp/tools/usage_tools.cpp src/mcp/tools/assertion_tools.cpp src/mcp/tools/tools.h src/mcp/mcp_server_default.cpp CMakeLists.txt
git commit -m "feat(mcp): register 8 remaining Phase 2 tools (mesh, snapshot, usage, 5 assertions)"
```

---

## Task 10: Integration Tests

**Files:**
- Create: `tests/integration/test_tools_phase2.cpp`
- Modify: `tests/integration/test_tools_phase1.cpp` (add Phase 2 tool registrations to fixture)

- [ ] **Step 1: Update Phase 1 test fixture to register Phase 2 tools**

In `tests/integration/test_tools_phase1.cpp`, add includes after line 4:

```cpp
#include "core/shader_edit.h"
#include "core/mesh.h"
#include "core/snapshot.h"
#include "core/usage.h"
#include "core/assertions.h"
```

In `SetUpTestSuite()` (after `tools::registerTexStatsTools(s_registry);`, line 57), add:

```cpp
        tools::registerShaderEditTools(s_registry);
        tools::registerMeshTools(s_registry);
        tools::registerSnapshotTools(s_registry);
        tools::registerUsageTools(s_registry);
        tools::registerAssertionTools(s_registry);
```

- [ ] **Step 2: Create test_tools_phase2.cpp**

```cpp
#include <gtest/gtest.h>
#include "mcp/tool_registry.h"
#include "mcp/tools/tools.h"
#include "core/session.h"

#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;
using renderdoc::core::Session;
using renderdoc::mcp::ToolRegistry;
namespace tools = renderdoc::mcp::tools;
namespace fs = std::filesystem;

#ifdef _WIN32
static void openCaptureImpl3(Session* s);

#pragma warning(push)
#pragma warning(disable: 4611)
static bool doOpenCaptureSEH3(Session* s)
{
    __try { openCaptureImpl3(s); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#pragma warning(pop)

static void openCaptureImpl3(Session* s) { s->open(TEST_RDC_PATH); }
#endif

class Phase2ToolTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
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
        tools::registerShaderEditTools(s_registry);
        tools::registerMeshTools(s_registry);
        tools::registerSnapshotTools(s_registry);
        tools::registerUsageTools(s_registry);
        tools::registerAssertionTools(s_registry);

#ifdef _WIN32
        if (!doOpenCaptureSEH3(&s_session)) { s_skipAll = true; return; }
#else
        s_session.open(TEST_RDC_PATH);
#endif
        ASSERT_TRUE(s_session.isOpen());

        auto draws = s_registry.callTool("list_draws", s_session, {});
        ASSERT_TRUE(draws.contains("draws"));
        ASSERT_GT(draws["draws"].size(), 0u);
        s_firstDrawEid = draws["draws"][0]["eventId"].get<uint32_t>();
        s_registry.callTool("goto_event", s_session, {{"eventId", s_firstDrawEid}});

        auto resources = s_registry.callTool("list_resources", s_session, {{"type", "Texture"}});
        if (resources.contains("resources") && resources["resources"].size() > 0)
            s_textureResId = resources["resources"][0]["resourceId"].get<std::string>();
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

Session Phase2ToolTest::s_session;
ToolRegistry Phase2ToolTest::s_registry;
uint32_t Phase2ToolTest::s_firstDrawEid = 0;
std::string Phase2ToolTest::s_textureResId;
bool Phase2ToolTest::s_skipAll = false;

// -- shader_encodings ---------------------------------------------------------

TEST_F(Phase2ToolTest, ShaderEncodings_ReturnsList) {
    auto result = s_registry.callTool("shader_encodings", s_session, {});
    EXPECT_TRUE(result.contains("encodings"));
    EXPECT_TRUE(result["encodings"].is_array());
    EXPECT_GT(result["encodings"].size(), 0u);
}

// -- export_mesh --------------------------------------------------------------

TEST_F(Phase2ToolTest, ExportMesh_ReturnsOBJ) {
    auto result = s_registry.callTool("export_mesh", s_session,
        {{"eventId", s_firstDrawEid}, {"format", "obj"}});
    EXPECT_TRUE(result.contains("obj"));
    EXPECT_TRUE(result.contains("vertexCount"));
    EXPECT_GT(result["vertexCount"].get<int>(), 0);
}

TEST_F(Phase2ToolTest, ExportMesh_ReturnsJSON) {
    auto result = s_registry.callTool("export_mesh", s_session,
        {{"eventId", s_firstDrawEid}, {"format", "json"}});
    EXPECT_TRUE(result.contains("vertices"));
    EXPECT_TRUE(result.contains("faces"));
    EXPECT_TRUE(result.contains("topology"));
}

// -- export_snapshot ----------------------------------------------------------

TEST_F(Phase2ToolTest, ExportSnapshot_CreatesFiles) {
    std::string tmpDir = (fs::temp_directory_path() / "rdc_snapshot_test").string();
    fs::remove_all(tmpDir);

    auto result = s_registry.callTool("export_snapshot", s_session,
        {{"eventId", s_firstDrawEid}, {"outputDir", tmpDir}});

    EXPECT_TRUE(result.contains("manifestPath"));
    EXPECT_TRUE(result.contains("files"));
    EXPECT_GT(result["files"].size(), 0u);
    EXPECT_TRUE(fs::exists(fs::path(tmpDir) / "manifest.json"));
    EXPECT_TRUE(fs::exists(fs::path(tmpDir) / "pipeline.json"));

    fs::remove_all(tmpDir);
}

// -- get_resource_usage -------------------------------------------------------

TEST_F(Phase2ToolTest, ResourceUsage_ReturnsList) {
    if (s_textureResId.empty()) GTEST_SKIP() << "No texture resource found";

    auto result = s_registry.callTool("get_resource_usage", s_session,
        {{"resourceId", s_textureResId}});
    EXPECT_TRUE(result.contains("entries"));
    EXPECT_TRUE(result["entries"].is_array());
}

// -- assert_pixel -------------------------------------------------------------

TEST_F(Phase2ToolTest, AssertPixel_MatchesPickPixel) {
    auto pick = s_registry.callTool("pick_pixel", s_session,
        {{"x", 0}, {"y", 0}, {"eventId", s_firstDrawEid}});

    float r = pick["color"]["floatValue"][0].get<float>();
    float g = pick["color"]["floatValue"][1].get<float>();
    float b = pick["color"]["floatValue"][2].get<float>();
    float a = pick["color"]["floatValue"][3].get<float>();

    auto result = s_registry.callTool("assert_pixel", s_session,
        {{"eventId", s_firstDrawEid}, {"x", 0}, {"y", 0},
         {"expected", {r, g, b, a}}, {"tolerance", 0.01}});

    EXPECT_TRUE(result["pass"].get<bool>());
}

TEST_F(Phase2ToolTest, AssertPixel_FailsOnWrongColor) {
    auto result = s_registry.callTool("assert_pixel", s_session,
        {{"eventId", s_firstDrawEid}, {"x", 0}, {"y", 0},
         {"expected", {-999.0, -999.0, -999.0, -999.0}}, {"tolerance", 0.001}});

    EXPECT_FALSE(result["pass"].get<bool>());
}

// -- assert_state -------------------------------------------------------------

TEST_F(Phase2ToolTest, AssertState_ChecksApi) {
    auto result = s_registry.callTool("assert_state", s_session,
        {{"eventId", s_firstDrawEid}, {"path", "api"}, {"expected", "Vulkan"}});

    EXPECT_TRUE(result["pass"].get<bool>());
}

TEST_F(Phase2ToolTest, AssertState_FailsOnWrongValue) {
    auto result = s_registry.callTool("assert_state", s_session,
        {{"eventId", s_firstDrawEid}, {"path", "api"}, {"expected", "D3D12"}});

    EXPECT_FALSE(result["pass"].get<bool>());
}

// -- assert_count -------------------------------------------------------------

TEST_F(Phase2ToolTest, AssertCount_DrawsGtZero) {
    auto result = s_registry.callTool("assert_count", s_session,
        {{"what", "draws"}, {"expected", 0}, {"op", "gt"}});

    EXPECT_TRUE(result["pass"].get<bool>());
}

TEST_F(Phase2ToolTest, AssertCount_EventsExact) {
    auto status = s_session.status();
    int totalEvents = static_cast<int>(status.totalEvents);

    auto result = s_registry.callTool("assert_count", s_session,
        {{"what", "events"}, {"expected", totalEvents}, {"op", "eq"}});

    EXPECT_TRUE(result["pass"].get<bool>());
}

// -- assert_clean -------------------------------------------------------------

TEST_F(Phase2ToolTest, AssertClean_ReturnsResult) {
    auto result = s_registry.callTool("assert_clean", s_session, {});
    EXPECT_TRUE(result.contains("pass"));
    EXPECT_TRUE(result.contains("count"));
    EXPECT_TRUE(result.contains("minSeverity"));
}
```

- [ ] **Step 3: Verify tests compile**

Run: `cmake --build build --config Release --target test-tools 2>&1 | tail -10`

- [ ] **Step 4: Run tests**

Run: `cd build && ctest -R Phase2 -V 2>&1 | tail -30`

Expected: All tests pass (or skip on headless machines).

- [ ] **Step 5: Commit**

```bash
git add tests/integration/test_tools_phase2.cpp tests/integration/test_tools_phase1.cpp
git commit -m "test: add integration tests for all 13 Phase 2 tools"
```

---

## Task 11: Update Protocol Test

**Files:**
- Modify: `tests/integration/test_protocol.cpp`

- [ ] **Step 1: Find and update tool count**

Search for the current tool count assertion (should be `27u`) and update to `40u`:

```cpp
EXPECT_EQ(tools.size(), 40u)
    << "Expected 40 tools, got " << tools.size();
```

- [ ] **Step 2: Verify compilation and test**

Run: `cmake --build build --config Release --target test-integration && cd build && ctest -R Protocol -V 2>&1 | tail -10`

- [ ] **Step 3: Commit**

```bash
git add tests/integration/test_protocol.cpp
git commit -m "test: update protocol test tool count from 27 to 40"
```

---

## Task 12: CLI Commands

**Files:**
- Modify: `src/cli/main.cpp`

- [ ] **Step 1: Add includes**

Add after existing includes at top of `src/cli/main.cpp`:

```cpp
#include "core/assertions.h"
#include "core/mesh.h"
#include "core/shader_edit.h"
#include "core/snapshot.h"
#include "core/usage.h"
```

- [ ] **Step 2: Update printUsage()**

Add to the usage message:

```cpp
              << "  shader-encodings\n"
              << "  shader-build FILE --stage STAGE --encoding ENC [--entry NAME]\n"
              << "  shader-replace EID STAGE --with SHADER_ID\n"
              << "  shader-restore EID STAGE\n"
              << "  shader-restore-all\n"
              << "  mesh EID [--stage vs-out|gs-out] [--format obj|json] [-o FILE]\n"
              << "  snapshot EID -o DIR\n"
              << "  usage RES_ID\n"
              << "  assert-pixel EID X Y --expect R G B A [--tolerance T] [--target N]\n"
              << "  assert-state EID PATH --expect VALUE\n"
              << "  assert-image EXPECTED ACTUAL [--threshold T] [--diff-output PATH]\n"
              << "  assert-count WHAT --expect N [--op eq|gt|lt|ge|le]\n"
              << "  assert-clean [--min-severity high|medium|low|info]\n";
```

- [ ] **Step 3: Add Args fields for Phase 2**

Add to the `Args` struct:

```cpp
    // Phase 2 additions
    std::string encoding;
    std::string entry = "main";
    uint64_t shaderId = 0;
    std::string format = "obj";
    std::string expectStr;
    std::vector<float> expectRGBA;
    float tolerance = 0.01f;
    std::string opStr = "eq";
    int expectCount = 0;
    std::string minSeverity = "high";
    std::string diffOutput;
    double threshold = 0.0;
```

- [ ] **Step 4: Add argument parsing for Phase 2 options**

In `parseArgs()`, add parsing for the new flags (`--encoding`, `--entry`, `--with`, `--expect`, `--tolerance`, `--op`, `--min-severity`, `--diff-output`, `--threshold`) following the existing pattern of `if/else if` chains.

- [ ] **Step 5: Add command handlers**

Add command handler blocks for each of the 13 new commands, following the existing pattern. Each block:
1. Opens capture with `session.open(a.capturePath)`
2. Calls the appropriate core function
3. Prints output to stdout
4. Returns 0 on success

Example for `shader-encodings`:

```cpp
    } else if (a.command == "shader-encodings") {
        Session session;
        session.open(a.capturePath);
        auto encodings = getShaderEncodings(session);
        for (const auto& e : encodings) std::cout << e << "\n";
```

Example for `assert-pixel`:

```cpp
    } else if (a.command == "assert-pixel") {
        Session session;
        session.open(a.capturePath);
        uint32_t eid = a.eventId.value_or(0);
        uint32_t x = std::stoul(a.positional.at(0));
        uint32_t y = std::stoul(a.positional.at(1));
        auto result = assertPixel(session, eid, x, y, a.expectRGBA, a.tolerance, a.targetIndex);
        std::cout << (result.pass ? "pass" : "FAIL") << ": " << result.message << "\n";
        return result.pass ? 0 : 1;
```

(Implement all 13 command handlers following this pattern.)

- [ ] **Step 6: Verify compilation**

Run: `cmake --build build --config Release --target renderdoc-cli 2>&1 | tail -5`

- [ ] **Step 7: Commit**

```bash
git add src/cli/main.cpp
git commit -m "feat(cli): add 13 Phase 2 CLI commands"
```

---

## Task 13: Documentation and Release

**Files:**
- Modify: `README.md`
- Modify: `skills/renderdoc-mcp/SKILL.md` (if exists)

- [ ] **Step 1: Update README with Phase 2 tools**

Add the 13 new tools to the tool table in README.md, organized by category.

- [ ] **Step 2: Update SKILL.md**

If `skills/renderdoc-mcp/SKILL.md` lists tools, add the 13 new tools.

- [ ] **Step 3: Update release notes**

Create or update release notes for v0.3.0.

- [ ] **Step 4: Final build and test**

```bash
cmake --build build --config Release 2>&1 | tail -5
cd build && ctest -V 2>&1 | tail -30
```

- [ ] **Step 5: Commit**

```bash
git add README.md skills/ release-notes/
git commit -m "docs: update README and SKILL.md for Phase 2 (40 tools)"
```
