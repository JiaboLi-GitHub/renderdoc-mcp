#include "tools.h"
#include "../tool_registry.h"
#include "../renderdoc_wrapper.h"
#include "renderdoc_replay.h"
#include <algorithm>
#include <cstring>
#include <sstream>

using json = nlohmann::json;

static std::string resourceIdToString(ResourceId id)
{
    if(id == ResourceId::Null())
        return "null";
    uint64_t raw = 0;
    static_assert(sizeof(ResourceId) == sizeof(uint64_t), "ResourceId size mismatch");
    memcpy(&raw, &id, sizeof(raw));
    std::ostringstream oss;
    oss << "ResourceId::" << raw;
    return oss.str();
}

static std::string graphicsApiToString(GraphicsAPI api)
{
    switch(api)
    {
        case GraphicsAPI::D3D11: return "D3D11";
        case GraphicsAPI::D3D12: return "D3D12";
        case GraphicsAPI::OpenGL: return "OpenGL";
        case GraphicsAPI::Vulkan: return "Vulkan";
        default: return "Unknown";
    }
}

static std::string gpuVendorToString(GPUVendor vendor)
{
    switch(vendor)
    {
        case GPUVendor::Unknown: return "Unknown";
        case GPUVendor::ARM: return "ARM";
        case GPUVendor::AMD: return "AMD";
        case GPUVendor::Broadcom: return "Broadcom";
        case GPUVendor::Imagination: return "Imagination";
        case GPUVendor::Intel: return "Intel";
        case GPUVendor::nVidia: return "nVidia";
        case GPUVendor::Qualcomm: return "Qualcomm";
        case GPUVendor::Samsung: return "Samsung";
        case GPUVendor::Verisilicon: return "Verisilicon";
        case GPUVendor::Software: return "Software";
        default: return "Other";
    }
}

static uint32_t countAllEvents(const rdcarray<ActionDescription>& actions)
{
    uint32_t count = 0;
    for(const auto& action : actions)
    {
        count++;
        if(!action.children.empty())
            count += countAllEvents(action.children);
    }
    return count;
}

static uint32_t countDrawCalls(const rdcarray<ActionDescription>& actions)
{
    uint32_t count = 0;
    for(const auto& action : actions)
    {
        if(bool(action.flags & ActionFlags::Drawcall))
            count++;
        if(!action.children.empty())
            count += countDrawCalls(action.children);
    }
    return count;
}

// --- Helpers for get_stats ---

struct PassStats
{
    std::string name;
    uint32_t drawCount = 0;
    uint32_t dispatchCount = 0;
    uint64_t totalTriangles = 0;
};

struct DrawInfo
{
    uint32_t eventId;
    std::string name;
    uint32_t numIndices;
};

static void collectDrawsRecursive(const rdcarray<ActionDescription>& actions,
                                  const SDFile& structuredFile,
                                  uint32_t& drawCount, uint32_t& dispatchCount,
                                  uint64_t& totalTriangles,
                                  std::vector<DrawInfo>& allDraws)
{
    for(const auto& action : actions)
    {
        if(bool(action.flags & ActionFlags::Drawcall))
        {
            drawCount++;
            totalTriangles += action.numIndices / 3;
            allDraws.push_back({action.eventId,
                                std::string(action.GetName(structuredFile).c_str()),
                                action.numIndices});
        }
        if(bool(action.flags & ActionFlags::Dispatch))
            dispatchCount++;

        if(!action.children.empty())
            collectDrawsRecursive(action.children, structuredFile,
                                  drawCount, dispatchCount, totalTriangles, allDraws);
    }
}

// --- Helpers for get_log ---

static std::string severityToString(MessageSeverity sev)
{
    switch(sev)
    {
        case MessageSeverity::High: return "HIGH";
        case MessageSeverity::Medium: return "MEDIUM";
        case MessageSeverity::Low: return "LOW";
        case MessageSeverity::Info: return "INFO";
        default: return "UNKNOWN";
    }
}

static int severityLevel(MessageSeverity sev)
{
    switch(sev)
    {
        case MessageSeverity::High: return 3;
        case MessageSeverity::Medium: return 2;
        case MessageSeverity::Low: return 1;
        case MessageSeverity::Info: return 0;
        default: return -1;
    }
}

static int parseSeverityLevel(const std::string& level)
{
    if(level == "HIGH") return 3;
    if(level == "MEDIUM") return 2;
    if(level == "LOW") return 1;
    if(level == "INFO") return 0;
    return -1;
}

static std::string categoryToString(MessageCategory cat)
{
    switch(cat)
    {
        case MessageCategory::Application_Defined: return "Application_Defined";
        case MessageCategory::Miscellaneous: return "Miscellaneous";
        case MessageCategory::Initialization: return "Initialization";
        case MessageCategory::Cleanup: return "Cleanup";
        case MessageCategory::Compilation: return "Compilation";
        case MessageCategory::State_Creation: return "State_Creation";
        case MessageCategory::State_Setting: return "State_Setting";
        case MessageCategory::State_Getting: return "State_Getting";
        case MessageCategory::Resource_Manipulation: return "Resource_Manipulation";
        case MessageCategory::Execution: return "Execution";
        case MessageCategory::Shaders: return "Shaders";
        case MessageCategory::Deprecated: return "Deprecated";
        case MessageCategory::Undefined: return "Undefined";
        case MessageCategory::Portability: return "Portability";
        case MessageCategory::Performance: return "Performance";
        default: return "Unknown";
    }
}

void registerInfoTools(ToolRegistry& registry)
{
    // get_capture_info
    registry.registerTool({
        "get_capture_info",
        "Get metadata about the currently opened capture file including API, GPU, resolution, event count, and driver info.",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            json result;

            // API properties
            APIProperties props = ctrl->GetAPIProperties();
            result["api"] = graphicsApiToString(props.pipelineType);
            result["degraded"] = props.degraded;

            // Event counts
            const auto& actions = ctrl->GetRootActions();
            result["eventCount"] = countAllEvents(actions);
            result["drawCallCount"] = countDrawCalls(actions);

            // Capture file path
            result["capturePath"] = w.getCapturePath();

            // ICaptureFile metadata
            auto* cap = w.getCaptureFile();
            if(cap)
            {
                result["driverName"] = std::string(cap->DriverName().c_str());
                result["machineIdent"] = std::string(cap->RecordedMachineIdent().c_str());
                result["hasCallstacks"] = cap->HasCallstacks();
                result["timestampBase"] = cap->TimestampBase();

                // GPU list
                auto gpus = cap->GetAvailableGPUs();
                json gpuArray = json::array();
                for(const auto& gpu : gpus)
                {
                    json g;
                    g["name"] = std::string(gpu.name.c_str());
                    g["vendor"] = gpuVendorToString(gpu.vendor);
                    g["deviceID"] = gpu.deviceID;
                    g["driver"] = std::string(gpu.driver.c_str());
                    gpuArray.push_back(g);
                }
                result["gpus"] = gpuArray;
            }

            return result;
        }
    });

    // get_stats
    registry.registerTool({
        "get_stats",
        "Get performance statistics: per-pass breakdown, top draws by triangle count, largest resources",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            const auto& rootActions = ctrl->GetRootActions();
            const SDFile& structuredFile = ctrl->GetStructuredFile();

            // Collect per-pass stats.
            // Top-level children with children are treated as passes (PushMarker regions).
            json perPass = json::array();
            std::vector<DrawInfo> allDraws;

            for(const auto& action : rootActions)
            {
                if(!action.children.empty())
                {
                    // This is a pass (marker region)
                    PassStats pass;
                    pass.name = std::string(action.GetName(structuredFile).c_str());
                    collectDrawsRecursive(action.children, structuredFile,
                                          pass.drawCount, pass.dispatchCount,
                                          pass.totalTriangles, allDraws);

                    json p;
                    p["name"] = pass.name;
                    p["drawCount"] = pass.drawCount;
                    p["dispatchCount"] = pass.dispatchCount;
                    p["totalTriangles"] = pass.totalTriangles;
                    perPass.push_back(p);
                }
                else
                {
                    // Top-level action without children — collect draws from it too
                    if(bool(action.flags & ActionFlags::Drawcall))
                    {
                        allDraws.push_back({action.eventId,
                                            std::string(action.GetName(structuredFile).c_str()),
                                            action.numIndices});
                    }
                }
            }

            // Top 5 draws by triangle count (numIndices / 3)
            std::sort(allDraws.begin(), allDraws.end(),
                      [](const DrawInfo& a, const DrawInfo& b) {
                          return a.numIndices > b.numIndices;
                      });

            json topDraws = json::array();
            for(size_t i = 0; i < std::min<size_t>(5, allDraws.size()); i++)
            {
                json d;
                d["eventId"] = allDraws[i].eventId;
                d["name"] = allDraws[i].name;
                d["numIndices"] = allDraws[i].numIndices;
                topDraws.push_back(d);
            }

            // Largest 5 resources by byteSize
            // Collect textures and buffers with their sizes
            struct ResEntry {
                std::string name;
                uint64_t byteSize;
                std::string type;
                uint32_t width;
                uint32_t height;
            };
            std::vector<ResEntry> sizedResources;

            // Build name lookup from ResourceDescription
            rdcarray<ResourceDescription> resDescs = ctrl->GetResources();
            auto getResName = [&](ResourceId id) -> std::string {
                for(const auto& r : resDescs)
                    if(r.resourceId == id)
                        return std::string(r.name.c_str());
                return resourceIdToString(id);
            };

            const auto& textures = ctrl->GetTextures();
            for(const auto& tex : textures)
            {
                sizedResources.push_back({
                    getResName(tex.resourceId),
                    tex.byteSize,
                    std::string(tex.format.Name().c_str()),
                    tex.width,
                    tex.height
                });
            }

            const auto& buffers = ctrl->GetBuffers();
            for(const auto& buf : buffers)
            {
                sizedResources.push_back({
                    getResName(buf.resourceId),
                    buf.length,
                    "Buffer",
                    0, 0
                });
            }

            std::sort(sizedResources.begin(), sizedResources.end(),
                      [](const ResEntry& a, const ResEntry& b) { return a.byteSize > b.byteSize; });

            json largestResources = json::array();
            for(size_t i = 0; i < std::min<size_t>(5, sizedResources.size()); i++)
            {
                const auto& res = sizedResources[i];
                json r;
                r["name"] = res.name;
                r["byteSize"] = res.byteSize;
                r["type"] = res.type;
                r["width"] = res.width;
                r["height"] = res.height;
                largestResources.push_back(r);
            }

            json result;
            result["perPass"] = perPass;
            result["topDraws"] = topDraws;
            result["largestResources"] = largestResources;
            return result;
        }
    });

    // get_log
    registry.registerTool({
        "get_log",
        "Get debug/validation messages from the capture",
        {
            {"type", "object"},
            {"properties", {
                {"level", {{"type", "string"},
                           {"enum", json::array({"HIGH", "MEDIUM", "LOW", "INFO"})},
                           {"description", "Minimum severity level"}}},
                {"eventId", {{"type", "integer"},
                             {"description", "Filter by event ID"}}}
            }}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            rdcarray<DebugMessage> msgs = ctrl->GetDebugMessages();

            int minLevel = -1;
            if(args.contains("level") && args["level"].is_string())
            {
                minLevel = parseSeverityLevel(args["level"].get<std::string>());
            }

            bool filterEventId = args.contains("eventId") && args["eventId"].is_number_integer();
            uint32_t targetEventId = 0;
            if(filterEventId)
                targetEventId = args["eventId"].get<uint32_t>();

            json messages = json::array();
            for(const auto& msg : msgs)
            {
                // Filter by severity: Higher severity = lower enum value in renderdoc.
                // severityLevel returns higher numbers for higher severity.
                if(minLevel >= 0 && severityLevel(msg.severity) < minLevel)
                    continue;

                if(filterEventId && msg.eventId != targetEventId)
                    continue;

                json m;
                m["eventId"] = msg.eventId;
                m["severity"] = severityToString(msg.severity);
                m["category"] = categoryToString(msg.category);
                m["message"] = std::string(msg.description.c_str());
                messages.push_back(m);
            }

            json result;
            result["messages"] = messages;
            result["count"] = messages.size();
            return result;
        }
    });
}
