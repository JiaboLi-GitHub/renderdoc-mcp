#include "core/diff.h"
#include "core/diff_session.h"
#include "core/errors.h"
#include "core/assertions.h"
#include "core/pipeline.h"

#include <renderdoc_replay.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace renderdoc::core {

// ---------------------------------------------------------------------------
// lcsAlign: Standard O(n*m) LCS DP with backtracking.
// Returns a list of (optA, optB) pairs where:
//   (some(i), some(j)) = matched
//   (some(i), none)    = deleted (only in A)
//   (none,    some(j)) = added   (only in B)
// ---------------------------------------------------------------------------
std::vector<AlignedPair> lcsAlign(const std::vector<std::string>& keysA,
                                   const std::vector<std::string>& keysB)
{
    const size_t n = keysA.size();
    const size_t m = keysB.size();

    // Handle empty sequences as edge cases
    if (n == 0 && m == 0) {
        return {};
    }
    if (n == 0) {
        std::vector<AlignedPair> result;
        result.reserve(m);
        for (size_t j = 0; j < m; ++j)
            result.emplace_back(std::nullopt, j);
        return result;
    }
    if (m == 0) {
        std::vector<AlignedPair> result;
        result.reserve(n);
        for (size_t i = 0; i < n; ++i)
            result.emplace_back(i, std::nullopt);
        return result;
    }

    // Build DP table: dp[i][j] = LCS length of keysA[0..i-1] vs keysB[0..j-1]
    // Use (n+1) x (m+1) table
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            if (keysA[i - 1] == keysB[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1] + 1;
            } else {
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
            }
        }
    }

    // Backtrack to build alignment
    std::vector<AlignedPair> result;
    size_t i = n, j = m;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && keysA[i - 1] == keysB[j - 1]) {
            result.emplace_back(i - 1, j - 1);
            --i; --j;
        } else if (j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j])) {
            result.emplace_back(std::nullopt, j - 1);
            --j;
        } else {
            result.emplace_back(i - 1, std::nullopt);
            --i;
        }
    }

    std::reverse(result.begin(), result.end());
    return result;
}

// ---------------------------------------------------------------------------
// makeDrawMatchKey
// ---------------------------------------------------------------------------
std::string makeDrawMatchKey(const DrawRecord& rec, bool hasMarkers)
{
    if (hasMarkers) {
        return rec.markerPath + "|" + rec.drawType;
    } else {
        return rec.drawType + "|" + rec.shaderHash + "|" + rec.topology;
    }
}

// ---------------------------------------------------------------------------
// Anonymous namespace: helper functions for diff implementations
// ---------------------------------------------------------------------------
namespace {

// Convert RenderDoc ResourceId to our uint64_t alias.
static uint64_t rdcIdToU64(::ResourceId id) {
    uint64_t raw = 0;
    static_assert(sizeof(::ResourceId) == sizeof(uint64_t), "ResourceId size mismatch");
    std::memcpy(&raw, &id, sizeof(raw));
    return raw;
}

std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::string actionTypeString(ActionFlags flags) {
    if (bool(flags & ActionFlags::Dispatch))       return "Dispatch";
    if (bool(flags & ActionFlags::Clear))          return "Clear";
    if (bool(flags & ActionFlags::Copy))           return "Copy";
    if (bool(flags & ActionFlags::Resolve))        return "Resolve";
    if (bool(flags & ActionFlags::Present))        return "Present";
    if (bool(flags & ActionFlags::Indexed) &&
        bool(flags & ActionFlags::Drawcall))       return "DrawIndexed";
    if (bool(flags & ActionFlags::Drawcall))       return "Draw";
    return "Other";
}

std::string topologyString(Topology topo) {
    switch (topo) {
        case Topology::TriangleList:  return "TriangleList";
        case Topology::TriangleStrip: return "TriangleStrip";
        case Topology::TriangleFan:   return "TriangleFan";
        case Topology::LineList:      return "LineList";
        case Topology::LineStrip:     return "LineStrip";
        case Topology::PointList:     return "PointList";
        default:                       return "Other";
    }
}

std::string shaderIdHash(::ResourceId id) {
    uint64_t raw = rdcIdToU64(id);
    std::ostringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << raw;
    return ss.str();
}

// Compute triangle count from numIndices/numInstances (assume TriangleList as default).
// A more accurate count would need pipeline topology, but for diff purposes this is sufficient.
uint64_t computeTriangles(uint32_t numIndices, uint32_t numInstances) {
    if (numIndices == 0) return 0;
    uint64_t trisPerInstance = numIndices / 3;  // assume TriangleList
    return trisPerInstance * std::max(1u, numInstances);
}

// Build the marker path string from root to current action.
// parentPath is the path of the parent group.
void buildDrawRecords(const ActionDescription& action,
                      const std::string& parentPath,
                      std::vector<DrawRecord>& out)
{
    bool isGroup = !action.children.empty();
    bool isDraw  = bool(action.flags & ActionFlags::Drawcall) ||
                   bool(action.flags & ActionFlags::Dispatch) ||
                   bool(action.flags & ActionFlags::Clear) ||
                   bool(action.flags & ActionFlags::Copy);

    std::string myName(action.customName.c_str());
    std::string myPath = parentPath.empty() ? myName : (parentPath + "/" + myName);

    if (isGroup) {
        // Recurse into children with updated path
        for (const auto& child : action.children) {
            buildDrawRecords(child, myPath, out);
        }
    } else if (isDraw) {
        DrawRecord rec;
        rec.eventId   = action.eventId;
        rec.drawType  = actionTypeString(action.flags);
        rec.markerPath = parentPath;  // parent group path, not including this draw name
        rec.topology  = "Unknown";  // will be populated if needed
        rec.instances = action.numInstances;
        rec.triangles = computeTriangles(action.numIndices, action.numInstances);
        // shaderHash populated later if no markers
        out.push_back(std::move(rec));
    }
}

std::vector<DrawRecord> collectDrawRecords(IReplayController* ctrl) {
    const auto& rootActions = ctrl->GetRootActions();

    std::vector<DrawRecord> records;
    for (const auto& action : rootActions) {
        buildDrawRecords(action, "", records);
    }

    // Check if any record has a non-empty markerPath
    bool anyMarker = false;
    for (const auto& r : records) {
        if (!r.markerPath.empty()) { anyMarker = true; break; }
    }

    if (!anyMarker) {
        // Second pass: for each draw, navigate to it and read VS+PS shader IDs + topology
        for (auto& rec : records) {
            ctrl->SetFrameEvent(rec.eventId, true);
            APIProperties props = ctrl->GetAPIProperties();

            ::ResourceId vsId, psId;
            Topology topo = Topology::Unknown;
            switch (props.pipelineType) {
                case GraphicsAPI::D3D11: {
                    const D3D11Pipe::State* ps = ctrl->GetD3D11PipelineState();
                    if (ps) {
                        vsId = ps->vertexShader.resourceId;
                        psId = ps->pixelShader.resourceId;
                        topo = ps->inputAssembly.topology;
                    }
                    break;
                }
                case GraphicsAPI::D3D12: {
                    const D3D12Pipe::State* ps = ctrl->GetD3D12PipelineState();
                    if (ps) {
                        vsId = ps->vertexShader.resourceId;
                        psId = ps->pixelShader.resourceId;
                        topo = ps->inputAssembly.topology;
                    }
                    break;
                }
                case GraphicsAPI::OpenGL: {
                    const GLPipe::State* ps = ctrl->GetGLPipelineState();
                    if (ps) {
                        vsId = ps->vertexShader.shaderResourceId;
                        psId = ps->fragmentShader.shaderResourceId;
                        topo = ps->vertexInput.topology;
                    }
                    break;
                }
                case GraphicsAPI::Vulkan: {
                    const VKPipe::State* ps = ctrl->GetVulkanPipelineState();
                    if (ps) {
                        vsId = ps->vertexShader.resourceId;
                        psId = ps->fragmentShader.resourceId;
                        topo = ps->inputAssembly.topology;
                    }
                    break;
                }
                default:
                    break;
            }
            rec.shaderHash = shaderIdHash(vsId) + "_" + shaderIdHash(psId);
            rec.topology = topologyString(topo);
        }
    }

    return records;
}

bool hasAnyMarker(const std::vector<DrawRecord>& records) {
    for (const auto& r : records) {
        if (!r.markerPath.empty()) return true;
    }
    return false;
}

// Find the last Drawcall-flagged action in the tree.
const ActionDescription* findLastDraw(const rdcarray<ActionDescription>& actions) {
    const ActionDescription* last = nullptr;
    for (int i = (int)actions.size() - 1; i >= 0; --i) {
        const auto& action = actions[i];
        if (!action.children.empty()) {
            const ActionDescription* child = findLastDraw(action.children);
            if (child) return child;
        }
        if (bool(action.flags & ActionFlags::Drawcall)) {
            return &action;
        }
    }
    return last;
}

// Count all events recursively
uint32_t countEvents(const rdcarray<ActionDescription>& actions) {
    uint32_t count = 0;
    for (const auto& a : actions) {
        count++;
        if (!a.children.empty())
            count += countEvents(a.children);
    }
    return count;
}

// Count draws recursively
uint32_t countDraws(const rdcarray<ActionDescription>& actions) {
    uint32_t count = 0;
    for (const auto& a : actions) {
        if (bool(a.flags & ActionFlags::Drawcall)) count++;
        if (!a.children.empty())
            count += countDraws(a.children);
    }
    return count;
}

// Count passes (top-level groups with draw children)
uint32_t countPasses(const rdcarray<ActionDescription>& actions) {
    uint32_t count = 0;
    for (const auto& a : actions) {
        if (!a.children.empty()) count++;
    }
    return count;
}

// Stage name helper
std::string stageName(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:   return "VS";
        case ShaderStage::Hull:     return "HS";
        case ShaderStage::Domain:   return "DS";
        case ShaderStage::Geometry: return "GS";
        case ShaderStage::Pixel:    return "PS";
        case ShaderStage::Compute:  return "CS";
        default:                    return "Unknown";
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// diffDraws
// ---------------------------------------------------------------------------
DrawsDiffResult diffDraws(DiffSession& session)
{
    IReplayController* ctrlA = session.controllerA();
    IReplayController* ctrlB = session.controllerB();
    if (!ctrlA || !ctrlB)
        throw CoreError(CoreError::Code::NoCaptureOpen, "DiffSession not open.");

    std::vector<DrawRecord> recsA = collectDrawRecords(ctrlA);
    std::vector<DrawRecord> recsB = collectDrawRecords(ctrlB);

    bool hasMarkersA = hasAnyMarker(recsA);
    bool hasMarkersB = hasAnyMarker(recsB);
    bool hasMarkers  = hasMarkersA || hasMarkersB;

    // Build match keys
    std::vector<std::string> keysA, keysB;
    keysA.reserve(recsA.size());
    keysB.reserve(recsB.size());
    for (const auto& r : recsA) keysA.push_back(makeDrawMatchKey(r, hasMarkers));
    for (const auto& r : recsB) keysB.push_back(makeDrawMatchKey(r, hasMarkers));

    // Count key occurrences to assess uniqueness
    std::unordered_map<std::string, int> keyCountA, keyCountB;
    for (const auto& k : keysA) keyCountA[k]++;
    for (const auto& k : keysB) keyCountB[k]++;

    // Run LCS alignment
    auto alignment = lcsAlign(keysA, keysB);

    DrawsDiffResult result;
    for (const auto& pair : alignment) {
        DrawDiffRow row;
        if (pair.first.has_value())  row.a = recsA[*pair.first];
        if (pair.second.has_value()) row.b = recsB[*pair.second];

        if (pair.first.has_value() && pair.second.has_value()) {
            // Matched — check if truly equal or modified
            const std::string& keyA = keysA[*pair.first];
            bool unique = (keyCountA[keyA] == 1 && keyCountB[keyA] == 1);
            row.confidence = unique ? "high" : "low";

            // Equal if same key (keys include topology+shader or markerPath+drawType)
            row.status = DiffStatus::Equal;
            result.unchanged++;
        } else if (pair.first.has_value()) {
            row.status = DiffStatus::Deleted;
            row.confidence = "high";
            result.deleted++;
        } else {
            row.status = DiffStatus::Added;
            row.confidence = "high";
            result.added++;
        }

        result.rows.push_back(std::move(row));
    }

    return result;
}

// ---------------------------------------------------------------------------
// diffResources
// ---------------------------------------------------------------------------
ResourcesDiffResult diffResources(DiffSession& session)
{
    IReplayController* ctrlA = session.controllerA();
    IReplayController* ctrlB = session.controllerB();
    if (!ctrlA || !ctrlB)
        throw CoreError(CoreError::Code::NoCaptureOpen, "DiffSession not open.");

    rdcarray<ResourceDescription> resA = ctrlA->GetResources();
    rdcarray<ResourceDescription> resB = ctrlB->GetResources();

    // Build name->ResourceDescription map (first occurrence, named only)
    std::unordered_map<std::string, const ResourceDescription*> namedA, namedB;
    for (const auto& r : resA) {
        std::string name = toLower(std::string(r.name.c_str()));
        if (!name.empty() && namedA.find(name) == namedA.end())
            namedA[name] = &r;
    }
    for (const auto& r : resB) {
        std::string name = toLower(std::string(r.name.c_str()));
        if (!name.empty() && namedB.find(name) == namedB.end())
            namedB[name] = &r;
    }

    ResourcesDiffResult result;
    std::unordered_map<std::string, bool> matched;

    // Match named resources
    for (const auto& [name, rdA] : namedA) {
        ResourceDiffRow row;
        row.name = std::string(rdA->name.c_str());
        row.typeA = std::string(ToStr(rdA->type).c_str());
        row.confidence = "high";

        auto it = namedB.find(name);
        if (it != namedB.end()) {
            const ResourceDescription* rdB = it->second;
            row.typeB = std::string(ToStr(rdB->type).c_str());
            row.status = (row.typeA == row.typeB) ? DiffStatus::Equal : DiffStatus::Modified;
            if (row.status == DiffStatus::Equal) result.unchanged++;
            else result.modified++;
            matched[name] = true;
        } else {
            row.status = DiffStatus::Deleted;
            result.deleted++;
        }
        result.rows.push_back(std::move(row));
    }

    // Resources in B not in A (added named)
    for (const auto& [name, rdB] : namedB) {
        if (matched.find(name) == matched.end()) {
            ResourceDiffRow row;
            row.name = std::string(rdB->name.c_str());
            row.typeB = std::string(ToStr(rdB->type).c_str());
            row.status = DiffStatus::Added;
            row.confidence = "high";
            result.added++;
            result.rows.push_back(std::move(row));
        }
    }

    // Match unnamed resources positionally within type groups
    // Group unnamed resources by type
    std::unordered_map<std::string, std::vector<const ResourceDescription*>> unnamedByTypeA, unnamedByTypeB;
    for (const auto& r : resA) {
        std::string name = toLower(std::string(r.name.c_str()));
        if (name.empty()) {
            std::string type = std::string(ToStr(r.type).c_str());
            unnamedByTypeA[type].push_back(&r);
        }
    }
    for (const auto& r : resB) {
        std::string name = toLower(std::string(r.name.c_str()));
        if (name.empty()) {
            std::string type = std::string(ToStr(r.type).c_str());
            unnamedByTypeB[type].push_back(&r);
        }
    }

    // Collect all type keys
    std::unordered_map<std::string, bool> typesSeen;
    for (const auto& [t, _] : unnamedByTypeA) typesSeen[t] = true;
    for (const auto& [t, _] : unnamedByTypeB) typesSeen[t] = true;

    for (const auto& [type, _] : typesSeen) {
        auto& listA = unnamedByTypeA[type];
        auto& listB = unnamedByTypeB[type];
        size_t common = std::min(listA.size(), listB.size());

        for (size_t i = 0; i < common; ++i) {
            ResourceDiffRow row;
            row.name = "(unnamed " + type + " #" + std::to_string(i) + ")";
            row.typeA = type;
            row.typeB = type;
            row.status = DiffStatus::Equal;
            row.confidence = "low";
            result.unchanged++;
            result.rows.push_back(std::move(row));
        }
        for (size_t i = common; i < listA.size(); ++i) {
            ResourceDiffRow row;
            row.name = "(unnamed " + type + " #" + std::to_string(i) + ")";
            row.typeA = type;
            row.status = DiffStatus::Deleted;
            row.confidence = "low";
            result.deleted++;
            result.rows.push_back(std::move(row));
        }
        for (size_t i = common; i < listB.size(); ++i) {
            ResourceDiffRow row;
            row.name = "(unnamed " + type + " #" + std::to_string(i) + ")";
            row.typeB = type;
            row.status = DiffStatus::Added;
            row.confidence = "low";
            result.added++;
            result.rows.push_back(std::move(row));
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// diffStats
// ---------------------------------------------------------------------------
StatsDiffResult diffStats(DiffSession& session)
{
    IReplayController* ctrlA = session.controllerA();
    IReplayController* ctrlB = session.controllerB();
    if (!ctrlA || !ctrlB)
        throw CoreError(CoreError::Code::NoCaptureOpen, "DiffSession not open.");

    // Collect per-pass stats from a controller.
    // A "pass" is a top-level action with children.
    struct PassStats {
        std::string name;
        uint32_t draws = 0;
        uint32_t dispatches = 0;
        uint64_t triangles = 0;
    };

    auto collectPassStats = [&](IReplayController* ctrl) -> std::vector<PassStats> {
        std::vector<PassStats> passes;
        const auto& rootActions = ctrl->GetRootActions();

        std::function<void(const rdcarray<ActionDescription>&, PassStats&)> accum;
        accum = [&](const rdcarray<ActionDescription>& actions, PassStats& ps) {
            for (const auto& a : actions) {
                if (bool(a.flags & ActionFlags::Drawcall)) {
                    ps.draws++;
                    ps.triangles += computeTriangles(a.numIndices, a.numInstances);
                }
                if (bool(a.flags & ActionFlags::Dispatch)) {
                    ps.dispatches++;
                }
                if (!a.children.empty())
                    accum(a.children, ps);
            }
        };

        for (const auto& action : rootActions) {
            if (action.children.empty()) continue;
            PassStats ps;
            ps.name = std::string(action.customName.c_str());
            accum(action.children, ps);
            passes.push_back(std::move(ps));
        }
        // If no groups, create a single "default" pass
        if (passes.empty()) {
            PassStats ps;
            ps.name = "(default)";
            accum(rootActions, ps);
            passes.push_back(std::move(ps));
        }
        return passes;
    };

    auto passesA = collectPassStats(ctrlA);
    auto passesB = collectPassStats(ctrlB);

    // Build name map for B
    std::unordered_map<std::string, const PassStats*> mapB;
    for (const auto& p : passesB) {
        std::string key = toLower(p.name);
        if (mapB.find(key) == mapB.end())
            mapB[key] = &p;
    }

    StatsDiffResult result;
    std::unordered_map<std::string, bool> matched;

    // Match passes from A
    for (const auto& pa : passesA) {
        PassDiffRow row;
        row.name = pa.name;
        row.drawsA = pa.draws;
        row.trianglesA = pa.triangles;
        row.dispatchesA = pa.dispatches;

        std::string key = toLower(pa.name);
        auto it = mapB.find(key);
        if (it != mapB.end()) {
            const PassStats* pb = it->second;
            row.drawsB = pb->draws;
            row.trianglesB = pb->triangles;
            row.dispatchesB = pb->dispatches;

            bool equal = (pa.draws == pb->draws &&
                          pa.dispatches == pb->dispatches &&
                          pa.triangles == pb->triangles);
            row.status = equal ? DiffStatus::Equal : DiffStatus::Modified;
            if (!equal) result.passesChanged++;
            result.drawsDelta += (int64_t)pb->draws - (int64_t)pa.draws;
            result.trianglesDelta += (int64_t)pb->triangles - (int64_t)pa.triangles;
            result.dispatchesDelta += (int64_t)pb->dispatches - (int64_t)pa.dispatches;
            matched[key] = true;
        } else {
            row.status = DiffStatus::Deleted;
            result.passesDeleted++;
            result.drawsDelta -= (int64_t)pa.draws;
            result.trianglesDelta -= (int64_t)pa.triangles;
            result.dispatchesDelta -= (int64_t)pa.dispatches;
        }
        result.rows.push_back(std::move(row));
    }

    // Passes in B not in A
    for (const auto& pb : passesB) {
        std::string key = toLower(pb.name);
        if (matched.find(key) == matched.end()) {
            PassDiffRow row;
            row.name = pb.name;
            row.drawsB = pb.draws;
            row.trianglesB = pb.triangles;
            row.dispatchesB = pb.dispatches;
            row.status = DiffStatus::Added;
            result.passesAdded++;
            result.drawsDelta += (int64_t)pb.draws;
            result.trianglesDelta += (int64_t)pb.triangles;
            result.dispatchesDelta += (int64_t)pb.dispatches;
            result.rows.push_back(std::move(row));
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// diffPipeline
// ---------------------------------------------------------------------------
PipelineDiffResult diffPipeline(DiffSession& session, const std::string& markerPath)
{
    IReplayController* ctrlA = session.controllerA();
    IReplayController* ctrlB = session.controllerB();
    if (!ctrlA || !ctrlB)
        throw CoreError(CoreError::Code::NoCaptureOpen, "DiffSession not open.");

    // Parse optional [N] index suffix from markerPath
    std::string path = markerPath;
    int nthMatch = 0;
    {
        auto bracketPos = path.rfind('[');
        if (bracketPos != std::string::npos && path.back() == ']') {
            std::string idxStr = path.substr(bracketPos + 1, path.size() - bracketPos - 2);
            try {
                nthMatch = std::stoi(idxStr);
            } catch (...) {
                nthMatch = 0;
            }
            path = path.substr(0, bracketPos);
        }
    }

    // Get draw alignment
    DrawsDiffResult draws = diffDraws(session);

    // Find the Nth matched pair at the given markerPath
    uint32_t eidA = 0, eidB = 0;
    int matchCount = 0;
    for (const auto& row : draws.rows) {
        if (row.status == DiffStatus::Equal && row.a.has_value() && row.b.has_value()) {
            bool pathMatch = path.empty() ||
                             toLower(row.a->markerPath).find(toLower(path)) != std::string::npos;
            if (pathMatch) {
                if (matchCount == nthMatch) {
                    eidA = row.a->eventId;
                    eidB = row.b->eventId;
                    break;
                }
                matchCount++;
            }
        }
    }

    if (eidA == 0 || eidB == 0)
        throw CoreError(CoreError::Code::InvalidEventId,
                        "No matched draw found for markerPath: " + markerPath);

    // Navigate both controllers
    ctrlA->SetFrameEvent(eidA, true);
    ctrlB->SetFrameEvent(eidB, true);

    // Get pipeline state from both controllers.
    // We use a helper lambda to extract state directly from the controller
    // (similar to getPipelineState but for IReplayController directly).
    auto extractState = [](IReplayController* ctrl) -> PipelineState {
        APIProperties props = ctrl->GetAPIProperties();
        PipelineState state;

        auto toResId = [](::ResourceId id) -> uint64_t {
            uint64_t raw = 0;
            std::memcpy(&raw, &id, sizeof(raw));
            return raw;
        };

        switch (props.pipelineType) {
            case GraphicsAPI::D3D11: {
                state.api = GraphicsApi::D3D11;
                const D3D11Pipe::State* ps = ctrl->GetD3D11PipelineState();
                if (!ps) break;
                auto addShader = [&](ShaderStage stage, ::ResourceId rid, const ::ShaderReflection* refl) {
                    PipelineState::ShaderBinding sb;
                    sb.stage = stage;
                    sb.shaderId = toResId(rid);
                    if (refl) sb.entryPoint = refl->entryPoint.c_str();
                    state.shaders.push_back(std::move(sb));
                };
                addShader(ShaderStage::Vertex, ps->vertexShader.resourceId, ps->vertexShader.reflection);
                addShader(ShaderStage::Pixel, ps->pixelShader.resourceId, ps->pixelShader.reflection);
                for (const auto& rt : ps->outputMerger.renderTargets) {
                    if (rt.resource == ::ResourceId::Null()) continue;
                    RenderTargetInfo rti;
                    rti.id = toResId(rt.resource);
                    rti.format = rt.format.Name().c_str();
                    state.renderTargets.push_back(std::move(rti));
                }
                if (ps->outputMerger.depthTarget.resource != ::ResourceId::Null()) {
                    RenderTargetInfo dti;
                    dti.id = toResId(ps->outputMerger.depthTarget.resource);
                    dti.format = ps->outputMerger.depthTarget.format.Name().c_str();
                    state.depthTarget = std::move(dti);
                }
                for (const auto& vp : ps->rasterizer.viewports) {
                    if (!vp.enabled) continue;
                    Viewport v{vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth};
                    state.viewports.push_back(v);
                }
                break;
            }
            case GraphicsAPI::D3D12: {
                state.api = GraphicsApi::D3D12;
                const D3D12Pipe::State* ps = ctrl->GetD3D12PipelineState();
                if (!ps) break;
                auto addShader = [&](ShaderStage stage, ::ResourceId rid, const ::ShaderReflection* refl) {
                    PipelineState::ShaderBinding sb;
                    sb.stage = stage;
                    sb.shaderId = toResId(rid);
                    if (refl) sb.entryPoint = refl->entryPoint.c_str();
                    state.shaders.push_back(std::move(sb));
                };
                addShader(ShaderStage::Vertex, ps->vertexShader.resourceId, ps->vertexShader.reflection);
                addShader(ShaderStage::Pixel, ps->pixelShader.resourceId, ps->pixelShader.reflection);
                for (const auto& rt : ps->outputMerger.renderTargets) {
                    if (rt.resource == ::ResourceId::Null()) continue;
                    RenderTargetInfo rti;
                    rti.id = toResId(rt.resource);
                    rti.format = rt.format.Name().c_str();
                    state.renderTargets.push_back(std::move(rti));
                }
                if (ps->outputMerger.depthTarget.resource != ::ResourceId::Null()) {
                    RenderTargetInfo dti;
                    dti.id = toResId(ps->outputMerger.depthTarget.resource);
                    dti.format = ps->outputMerger.depthTarget.format.Name().c_str();
                    state.depthTarget = std::move(dti);
                }
                for (const auto& vp : ps->rasterizer.viewports) {
                    if (!vp.enabled) continue;
                    Viewport v{vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth};
                    state.viewports.push_back(v);
                }
                break;
            }
            case GraphicsAPI::OpenGL: {
                state.api = GraphicsApi::OpenGL;
                const GLPipe::State* ps = ctrl->GetGLPipelineState();
                if (!ps) break;
                auto addShader = [&](ShaderStage stage, ::ResourceId rid, const ::ShaderReflection* refl) {
                    PipelineState::ShaderBinding sb;
                    sb.stage = stage;
                    sb.shaderId = toResId(rid);
                    if (refl) sb.entryPoint = refl->entryPoint.c_str();
                    state.shaders.push_back(std::move(sb));
                };
                addShader(ShaderStage::Vertex, ps->vertexShader.shaderResourceId, ps->vertexShader.reflection);
                addShader(ShaderStage::Pixel, ps->fragmentShader.shaderResourceId, ps->fragmentShader.reflection);
                for (const auto& att : ps->framebuffer.drawFBO.colorAttachments) {
                    if (att.resource == ::ResourceId::Null()) continue;
                    RenderTargetInfo rti;
                    rti.id = toResId(att.resource);
                    state.renderTargets.push_back(std::move(rti));
                }
                if (ps->framebuffer.drawFBO.depthAttachment.resource != ::ResourceId::Null()) {
                    RenderTargetInfo dti;
                    dti.id = toResId(ps->framebuffer.drawFBO.depthAttachment.resource);
                    state.depthTarget = std::move(dti);
                }
                for (const auto& vp : ps->rasterizer.viewports) {
                    if (!vp.enabled) continue;
                    Viewport v{vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth};
                    state.viewports.push_back(v);
                }
                break;
            }
            case GraphicsAPI::Vulkan: {
                state.api = GraphicsApi::Vulkan;
                const VKPipe::State* ps = ctrl->GetVulkanPipelineState();
                if (!ps) break;
                auto addShader = [&](ShaderStage stage, ::ResourceId rid, const ::ShaderReflection* refl) {
                    PipelineState::ShaderBinding sb;
                    sb.stage = stage;
                    sb.shaderId = toResId(rid);
                    if (refl) sb.entryPoint = refl->entryPoint.c_str();
                    state.shaders.push_back(std::move(sb));
                };
                addShader(ShaderStage::Vertex, ps->vertexShader.resourceId, ps->vertexShader.reflection);
                addShader(ShaderStage::Pixel, ps->fragmentShader.resourceId, ps->fragmentShader.reflection);
                const auto& fb = ps->currentPass.framebuffer;
                for (uint32_t attIdx : ps->currentPass.renderpass.colorAttachments) {
                    if (attIdx < (uint32_t)fb.attachments.size()) {
                        const auto& att = fb.attachments[attIdx];
                        RenderTargetInfo rti;
                        rti.id = toResId(att.resource);
                        rti.format = att.format.Name().c_str();
                        state.renderTargets.push_back(std::move(rti));
                    }
                }
                uint32_t depthIdx = ps->currentPass.renderpass.depthstencilAttachment;
                if (depthIdx < (uint32_t)fb.attachments.size()) {
                    const auto& att = fb.attachments[depthIdx];
                    if (att.resource != ::ResourceId::Null()) {
                        RenderTargetInfo dti;
                        dti.id = toResId(att.resource);
                        dti.format = att.format.Name().c_str();
                        state.depthTarget = std::move(dti);
                    }
                }
                for (const auto& vps : ps->viewportScissor.viewportScissors) {
                    Viewport v{vps.vp.x, vps.vp.y, vps.vp.width, vps.vp.height,
                               vps.vp.minDepth, vps.vp.maxDepth};
                    state.viewports.push_back(v);
                }
                break;
            }
            default:
                break;
        }
        return state;
    };

    PipelineState stateA = extractState(ctrlA);
    PipelineState stateB = extractState(ctrlB);

    PipelineDiffResult result;
    result.eidA = eidA;
    result.eidB = eidB;
    result.markerPath = markerPath;

    auto addField = [&](const std::string& section, const std::string& field,
                        const std::string& va, const std::string& vb) {
        PipeFieldDiff f;
        f.section = section;
        f.field = field;
        f.valueA = va;
        f.valueB = vb;
        f.changed = (va != vb);
        if (f.changed) result.changedCount++;
        result.totalCount++;
        result.fields.push_back(std::move(f));
    };

    // Compare shader bindings
    size_t numShaders = std::max(stateA.shaders.size(), stateB.shaders.size());
    for (size_t i = 0; i < numShaders; ++i) {
        std::string section = "Shader[" + std::to_string(i) + "]";
        std::string stageA = (i < stateA.shaders.size()) ? stageName(stateA.shaders[i].stage) : "";
        std::string stageB = (i < stateB.shaders.size()) ? stageName(stateB.shaders[i].stage) : "";
        addField(section, "stage", stageA, stageB);

        std::string idA = (i < stateA.shaders.size()) ? std::to_string(stateA.shaders[i].shaderId) : "";
        std::string idB = (i < stateB.shaders.size()) ? std::to_string(stateB.shaders[i].shaderId) : "";
        addField(section, "shaderId", idA, idB);

        std::string epA = (i < stateA.shaders.size()) ? stateA.shaders[i].entryPoint : "";
        std::string epB = (i < stateB.shaders.size()) ? stateB.shaders[i].entryPoint : "";
        addField(section, "entryPoint", epA, epB);
    }

    // Compare render targets
    size_t numRTs = std::max(stateA.renderTargets.size(), stateB.renderTargets.size());
    for (size_t i = 0; i < numRTs; ++i) {
        std::string section = "RT[" + std::to_string(i) + "]";
        std::string idA = (i < stateA.renderTargets.size()) ? std::to_string(stateA.renderTargets[i].id) : "";
        std::string idB = (i < stateB.renderTargets.size()) ? std::to_string(stateB.renderTargets[i].id) : "";
        addField(section, "id", idA, idB);

        std::string nameA = (i < stateA.renderTargets.size()) ? stateA.renderTargets[i].name : "";
        std::string nameB = (i < stateB.renderTargets.size()) ? stateB.renderTargets[i].name : "";
        addField(section, "name", nameA, nameB);

        std::string wA = (i < stateA.renderTargets.size()) ? std::to_string(stateA.renderTargets[i].width) : "";
        std::string wB = (i < stateB.renderTargets.size()) ? std::to_string(stateB.renderTargets[i].width) : "";
        addField(section, "width", wA, wB);

        std::string hA = (i < stateA.renderTargets.size()) ? std::to_string(stateA.renderTargets[i].height) : "";
        std::string hB = (i < stateB.renderTargets.size()) ? std::to_string(stateB.renderTargets[i].height) : "";
        addField(section, "height", hA, hB);

        std::string fmtA = (i < stateA.renderTargets.size()) ? stateA.renderTargets[i].format : "";
        std::string fmtB = (i < stateB.renderTargets.size()) ? stateB.renderTargets[i].format : "";
        addField(section, "format", fmtA, fmtB);
    }

    // Compare depth target
    {
        std::string idA = stateA.depthTarget ? std::to_string(stateA.depthTarget->id) : "";
        std::string idB = stateB.depthTarget ? std::to_string(stateB.depthTarget->id) : "";
        addField("Depth", "id", idA, idB);

        std::string fmtA = stateA.depthTarget ? stateA.depthTarget->format : "";
        std::string fmtB = stateB.depthTarget ? stateB.depthTarget->format : "";
        addField("Depth", "format", fmtA, fmtB);
    }

    // Compare viewports
    size_t numVPs = std::max(stateA.viewports.size(), stateB.viewports.size());
    for (size_t i = 0; i < numVPs; ++i) {
        std::string section = "VP[" + std::to_string(i) + "]";
        auto vpStr = [](const Viewport& v) -> std::string {
            std::ostringstream ss;
            ss << v.x << "," << v.y << "," << v.width << "," << v.height;
            return ss.str();
        };
        std::string vA = (i < stateA.viewports.size()) ? vpStr(stateA.viewports[i]) : "";
        std::string vB = (i < stateB.viewports.size()) ? vpStr(stateB.viewports[i]) : "";
        addField(section, "rect", vA, vB);
    }

    return result;
}

// ---------------------------------------------------------------------------
// diffFramebuffer
// ---------------------------------------------------------------------------
ImageCompareResult diffFramebuffer(DiffSession& session,
                                    uint32_t eidA, uint32_t eidB,
                                    int target,
                                    double threshold,
                                    const std::string& diffOutput)
{
    IReplayController* ctrlA = session.controllerA();
    IReplayController* ctrlB = session.controllerB();
    if (!ctrlA || !ctrlB)
        throw CoreError(CoreError::Code::NoCaptureOpen, "DiffSession not open.");

    // If eidA == 0, find last draw in capture A
    if (eidA == 0) {
        const auto& rootA = ctrlA->GetRootActions();
        const ActionDescription* last = findLastDraw(rootA);
        if (last) eidA = last->eventId;
        else throw CoreError(CoreError::Code::InvalidEventId, "No draw found in capture A.");
    }
    if (eidB == 0) {
        const auto& rootB = ctrlB->GetRootActions();
        const ActionDescription* last = findLastDraw(rootB);
        if (last) eidB = last->eventId;
        else throw CoreError(CoreError::Code::InvalidEventId, "No draw found in capture B.");
    }

    // Navigate both controllers
    ctrlA->SetFrameEvent(eidA, true);
    ctrlB->SetFrameEvent(eidB, true);

    // Find the RT resource ID for each event
    auto findRtResourceId = [](IReplayController* ctrl, uint32_t eid, int rtIdx) -> ::ResourceId {
        const auto& actions = ctrl->GetRootActions();
        std::function<const ActionDescription*(const rdcarray<ActionDescription>&)> find;
        find = [&](const rdcarray<ActionDescription>& acts) -> const ActionDescription* {
            for (const auto& a : acts) {
                if (a.eventId == eid) return &a;
                if (!a.children.empty()) {
                    const ActionDescription* f = find(a.children);
                    if (f) return f;
                }
            }
            return nullptr;
        };
        const ActionDescription* action = find(actions);
        if (!action) return ::ResourceId::Null();
        if (rtIdx >= 0 && rtIdx < 8) return action->outputs[rtIdx];
        return ::ResourceId::Null();
    };

    ::ResourceId rtA = findRtResourceId(ctrlA, eidA, target);
    ::ResourceId rtB = findRtResourceId(ctrlB, eidB, target);

    if (rtA == ::ResourceId::Null())
        throw CoreError(CoreError::Code::ExportFailed,
                        "No RT at index " + std::to_string(target) + " for event " + std::to_string(eidA));
    if (rtB == ::ResourceId::Null())
        throw CoreError(CoreError::Code::ExportFailed,
                        "No RT at index " + std::to_string(target) + " for event " + std::to_string(eidB));

    // Create temp directory for PNGs
    fs::path tmpDir = fs::temp_directory_path() / "renderdoc_diff";
    fs::create_directories(tmpDir);

    std::string pathA = (tmpDir / ("diff_A_" + std::to_string(eidA) + "_rt" + std::to_string(target) + ".png")).string();
    std::string pathB = (tmpDir / ("diff_B_" + std::to_string(eidB) + "_rt" + std::to_string(target) + ".png")).string();

    // Save A
    {
        TextureSave saveData = {};
        saveData.resourceId = rtA;
        saveData.destType   = FileType::PNG;
        saveData.alpha      = AlphaMapping::Preserve;
        ResultDetails res = ctrlA->SaveTexture(saveData, rdcstr(pathA.c_str()));
        if (!res.OK())
            throw CoreError(CoreError::Code::ExportFailed,
                            "Failed to save RT A: " + std::string(res.Message().c_str()));
    }

    // Save B
    {
        TextureSave saveData = {};
        saveData.resourceId = rtB;
        saveData.destType   = FileType::PNG;
        saveData.alpha      = AlphaMapping::Preserve;
        ResultDetails res = ctrlB->SaveTexture(saveData, rdcstr(pathB.c_str()));
        if (!res.OK())
            throw CoreError(CoreError::Code::ExportFailed,
                            "Failed to save RT B: " + std::string(res.Message().c_str()));
    }

    // Compare using assertImage
    return assertImage(pathA, pathB, threshold, diffOutput);
}

// ---------------------------------------------------------------------------
// diffSummary
// ---------------------------------------------------------------------------
SummaryDiffResult diffSummary(DiffSession& session)
{
    IReplayController* ctrlA = session.controllerA();
    IReplayController* ctrlB = session.controllerB();
    if (!ctrlA || !ctrlB)
        throw CoreError(CoreError::Code::NoCaptureOpen, "DiffSession not open.");

    SummaryDiffResult result;

    const auto& rootA = ctrlA->GetRootActions();
    const auto& rootB = ctrlB->GetRootActions();

    // Level 1: Count metrics
    uint32_t drawsA   = countDraws(rootA);
    uint32_t drawsB   = countDraws(rootB);
    uint32_t passesA  = countPasses(rootA);
    uint32_t passesB  = countPasses(rootB);
    uint32_t eventsA  = countEvents(rootA);
    uint32_t eventsB  = countEvents(rootB);

    rdcarray<ResourceDescription> resA = ctrlA->GetResources();
    rdcarray<ResourceDescription> resB = ctrlB->GetResources();
    int resourcesA = resA.count();
    int resourcesB = resB.count();

    auto addRow = [&](const std::string& cat, int a, int b) {
        SummaryRow row;
        row.category = cat;
        row.valueA = a;
        row.valueB = b;
        row.delta = b - a;
        result.rows.push_back(row);
    };

    addRow("draws",     (int)drawsA,     (int)drawsB);
    addRow("passes",    (int)passesA,    (int)passesB);
    addRow("events",    (int)eventsA,    (int)eventsB);
    addRow("resources", resourcesA,       resourcesB);

    bool countsMatch = (drawsA == drawsB && passesA == passesB &&
                        eventsA == eventsB && resourcesA == resourcesB);

    if (!countsMatch) {
        result.divergedAt = "counts";
        result.identical = false;
        return result;
    }

    // Level 2: Structure check via diffDraws + diffResources
    DrawsDiffResult drawDiff = diffDraws(session);
    bool structureMatch = (drawDiff.added == 0 && drawDiff.deleted == 0 && drawDiff.modified == 0);

    if (!structureMatch) {
        result.divergedAt = "structure";
        result.identical = false;
        return result;
    }

    // Level 3: Framebuffer comparison at last draw
    try {
        ImageCompareResult imgResult = diffFramebuffer(session, 0, 0, 0, 0.0, "");
        if (!imgResult.pass) {
            result.divergedAt = "framebuffer";
            result.identical = false;
            return result;
        }
    } catch (const CoreError&) {
        // If framebuffer comparison fails (e.g. no RT), treat as structure match only
        result.identical = true;
        result.divergedAt = "";
        return result;
    }

    result.identical = true;
    result.divergedAt = "";
    return result;
}

} // namespace renderdoc::core
