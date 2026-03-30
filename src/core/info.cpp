#include "core/info.h"
#include "core/errors.h"
#include "core/session.h"

#include <renderdoc_replay.h>

#include <algorithm>
#include <cstring>

namespace renderdoc::core {

namespace {

// --- GraphicsAPI conversion ---

GraphicsApi apiFromRenderDoc(GraphicsAPI api) {
    switch (api) {
        case GraphicsAPI::D3D11:  return GraphicsApi::D3D11;
        case GraphicsAPI::D3D12:  return GraphicsApi::D3D12;
        case GraphicsAPI::OpenGL: return GraphicsApi::OpenGL;
        case GraphicsAPI::Vulkan: return GraphicsApi::Vulkan;
        default:                  return GraphicsApi::Unknown;
    }
}

// --- GPU vendor conversion ---

std::string gpuVendorToString(GPUVendor vendor) {
    switch (vendor) {
        case GPUVendor::Unknown:     return "Unknown";
        case GPUVendor::ARM:         return "ARM";
        case GPUVendor::AMD:         return "AMD";
        case GPUVendor::Broadcom:    return "Broadcom";
        case GPUVendor::Imagination: return "Imagination";
        case GPUVendor::Intel:       return "Intel";
        case GPUVendor::nVidia:      return "nVidia";
        case GPUVendor::Qualcomm:    return "Qualcomm";
        case GPUVendor::Samsung:     return "Samsung";
        case GPUVendor::Verisilicon: return "Verisilicon";
        case GPUVendor::Software:    return "Software";
        default:                     return "Other";
    }
}

// --- Event counting helpers ---

uint32_t countAllEvents(const rdcarray<ActionDescription>& actions) {
    uint32_t count = 0;
    for (const auto& action : actions) {
        count++;
        if (!action.children.empty())
            count += countAllEvents(action.children);
    }
    return count;
}

uint32_t countDrawCalls(const rdcarray<ActionDescription>& actions) {
    uint32_t count = 0;
    for (const auto& action : actions) {
        if (bool(action.flags & ActionFlags::Drawcall))
            count++;
        if (!action.children.empty())
            count += countDrawCalls(action.children);
    }
    return count;
}

// --- Stats helpers ---

struct DrawInfo {
    uint32_t eventId;
    std::string name;
    uint32_t numIndices;
};

void collectDrawsRecursive(const rdcarray<ActionDescription>& actions,
                            const SDFile& structuredFile,
                            uint32_t& drawCount,
                            uint32_t& dispatchCount,
                            uint64_t& totalTriangles,
                            std::vector<DrawInfo>& allDraws) {
    for (const auto& action : actions) {
        if (bool(action.flags & ActionFlags::Drawcall)) {
            drawCount++;
            totalTriangles += action.numIndices / 3;
            allDraws.push_back({action.eventId,
                                std::string(action.GetName(structuredFile).c_str()),
                                action.numIndices});
        }
        if (bool(action.flags & ActionFlags::Dispatch))
            dispatchCount++;

        if (!action.children.empty())
            collectDrawsRecursive(action.children, structuredFile,
                                  drawCount, dispatchCount, totalTriangles, allDraws);
    }
}

// --- Debug message helpers ---

std::string severityToString(MessageSeverity sev) {
    switch (sev) {
        case MessageSeverity::High:   return "HIGH";
        case MessageSeverity::Medium: return "MEDIUM";
        case MessageSeverity::Low:    return "LOW";
        case MessageSeverity::Info:   return "INFO";
        default:                      return "UNKNOWN";
    }
}

int severityLevel(MessageSeverity sev) {
    switch (sev) {
        case MessageSeverity::High:   return 3;
        case MessageSeverity::Medium: return 2;
        case MessageSeverity::Low:    return 1;
        case MessageSeverity::Info:   return 0;
        default:                      return -1;
    }
}

int parseSeverityLevel(const std::string& level) {
    if (level == "HIGH")   return 3;
    if (level == "MEDIUM") return 2;
    if (level == "LOW")    return 1;
    if (level == "INFO")   return 0;
    return -1;
}

std::string categoryToString(MessageCategory cat) {
    switch (cat) {
        case MessageCategory::Application_Defined:  return "Application_Defined";
        case MessageCategory::Miscellaneous:        return "Miscellaneous";
        case MessageCategory::Initialization:       return "Initialization";
        case MessageCategory::Cleanup:              return "Cleanup";
        case MessageCategory::Compilation:          return "Compilation";
        case MessageCategory::State_Creation:       return "State_Creation";
        case MessageCategory::State_Setting:        return "State_Setting";
        case MessageCategory::State_Getting:        return "State_Getting";
        case MessageCategory::Resource_Manipulation: return "Resource_Manipulation";
        case MessageCategory::Execution:            return "Execution";
        case MessageCategory::Shaders:              return "Shaders";
        case MessageCategory::Deprecated:           return "Deprecated";
        case MessageCategory::Undefined:            return "Undefined";
        case MessageCategory::Portability:          return "Portability";
        case MessageCategory::Performance:          return "Performance";
        default:                                    return "Unknown";
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------

CaptureInfo getCaptureInfo(const Session& session) {
    auto* ctrl = session.controller();
    CaptureInfo info;

    // API properties
    APIProperties props = ctrl->GetAPIProperties();
    info.api = apiFromRenderDoc(props.pipelineType);
    info.degraded = props.degraded;

    // Event counts
    const auto& actions = ctrl->GetRootActions();
    info.totalEvents = countAllEvents(actions);
    info.totalDraws = countDrawCalls(actions);

    // Capture path
    info.path = session.capturePath();

    // ICaptureFile metadata
    auto* cap = session.captureFile();
    if (cap) {
        info.driverName    = std::string(cap->DriverName().c_str());
        info.machineIdent  = std::string(cap->RecordedMachineIdent().c_str());
        info.hasCallstacks = cap->HasCallstacks();
        info.timestampBase = cap->TimestampBase();

        auto gpus = cap->GetAvailableGPUs();
        for (const auto& gpu : gpus) {
            CaptureInfo::GpuInfo g;
            g.name     = std::string(gpu.name.c_str());
            g.vendor   = gpuVendorToString(gpu.vendor);
            g.deviceID = gpu.deviceID;
            g.driver   = std::string(gpu.driver.c_str());
            info.gpus.push_back(std::move(g));
        }
    }

    return info;
}

// ---------------------------------------------------------------------------

CaptureStats getStats(const Session& session) {
    auto* ctrl = session.controller();

    const auto& rootActions    = ctrl->GetRootActions();
    const SDFile& structuredFile = ctrl->GetStructuredFile();

    CaptureStats stats;
    std::vector<DrawInfo> allDraws;

    // Per-pass stats: top-level marker regions (children non-empty = pass).
    for (const auto& action : rootActions) {
        if (!action.children.empty()) {
            PerPassStats pass;
            pass.name = std::string(action.GetName(structuredFile).c_str());

            uint32_t draws = 0, dispatches = 0;
            uint64_t tris = 0;
            collectDrawsRecursive(action.children, structuredFile,
                                  draws, dispatches, tris, allDraws);
            pass.drawCount     = draws;
            pass.dispatchCount = dispatches;
            pass.totalTriangles = tris;
            stats.perPass.push_back(std::move(pass));
        } else {
            // Top-level draw without a marker group
            if (bool(action.flags & ActionFlags::Drawcall)) {
                allDraws.push_back({action.eventId,
                                    std::string(action.GetName(structuredFile).c_str()),
                                    action.numIndices});
            }
        }
    }

    // Top 5 draws by triangle count (numIndices / 3 proxy).
    std::sort(allDraws.begin(), allDraws.end(),
              [](const DrawInfo& a, const DrawInfo& b) {
                  return a.numIndices > b.numIndices;
              });

    const size_t topN = std::min<size_t>(5, allDraws.size());
    for (size_t i = 0; i < topN; i++) {
        TopDraw td;
        td.eventId   = allDraws[i].eventId;
        td.name      = allDraws[i].name;
        td.numIndices = allDraws[i].numIndices;
        stats.topDraws.push_back(std::move(td));
    }

    // Largest 5 resources by byte size (textures + buffers).
    struct ResEntry {
        std::string name;
        uint64_t byteSize;
        std::string type;
        uint32_t width;
        uint32_t height;
    };
    std::vector<ResEntry> sizedResources;

    rdcarray<ResourceDescription> resDescs = ctrl->GetResources();
    auto getResName = [&](::ResourceId id) -> std::string {
        for (const auto& r : resDescs)
            if (r.resourceId == id)
                return std::string(r.name.c_str());
        // Fallback: encode as ResourceId::N
        uint64_t raw = 0;
        static_assert(sizeof(::ResourceId) == sizeof(uint64_t), "ResourceId size mismatch");
        std::memcpy(&raw, &id, sizeof(raw));
        return "ResourceId::" + std::to_string(raw);
    };

    const auto& textures = ctrl->GetTextures();
    for (const auto& tex : textures) {
        sizedResources.push_back({getResName(tex.resourceId),
                                  tex.byteSize,
                                  std::string(tex.format.Name().c_str()),
                                  tex.width,
                                  tex.height});
    }

    const auto& buffers = ctrl->GetBuffers();
    for (const auto& buf : buffers) {
        sizedResources.push_back({getResName(buf.resourceId),
                                  buf.length,
                                  "Buffer",
                                  0, 0});
    }

    std::sort(sizedResources.begin(), sizedResources.end(),
              [](const ResEntry& a, const ResEntry& b) { return a.byteSize > b.byteSize; });

    const size_t resN = std::min<size_t>(5, sizedResources.size());
    for (size_t i = 0; i < resN; i++) {
        LargestResource lr;
        lr.name     = sizedResources[i].name;
        lr.byteSize = sizedResources[i].byteSize;
        lr.type     = sizedResources[i].type;
        lr.width    = sizedResources[i].width;
        lr.height   = sizedResources[i].height;
        stats.largestResources.push_back(std::move(lr));
    }

    return stats;
}

// ---------------------------------------------------------------------------

std::vector<DebugMessage> getLog(const Session& session,
                                  const std::string& minSeverity,
                                  std::optional<uint32_t> eventId) {
    auto* ctrl = session.controller();

    rdcarray<::DebugMessage> msgs = ctrl->GetDebugMessages();

    int minLevel = parseSeverityLevel(minSeverity); // -1 means no filter

    std::vector<DebugMessage> result;
    for (const auto& msg : msgs) {
        if (minLevel >= 0 && severityLevel(msg.severity) < minLevel)
            continue;
        if (eventId.has_value() && msg.eventId != *eventId)
            continue;

        DebugMessage dm;
        dm.eventId  = msg.eventId;
        dm.severity = severityToString(msg.severity);
        dm.category = categoryToString(msg.category);
        dm.message  = std::string(msg.description.c_str());
        result.push_back(std::move(dm));
    }

    return result;
}

} // namespace renderdoc::core
