#include "core/diff_internal.h"
#include "core/errors.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_map>

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
// diff_internal helper function implementations
// ---------------------------------------------------------------------------
namespace diff_internal {

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
    uint64_t raw = toResourceId(id);
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

} // namespace diff_internal
} // namespace renderdoc::core
