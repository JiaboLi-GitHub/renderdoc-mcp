#include "tools.h"
#include "../tool_registry.h"
#include "../renderdoc_wrapper.h"
#include "renderdoc_replay.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <map>
#include <set>

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

// Helper struct holding reflection + resourceId for a given stage
struct ShaderStageInfo
{
    const ShaderReflection* reflection = nullptr;
    ResourceId resourceId;
};

// Get shader info for a given stage string across all APIs
static ShaderStageInfo getShaderStageInfo(IReplayController* ctrl, const std::string& stage)
{
    APIProperties props = ctrl->GetAPIProperties();
    ShaderStageInfo info;

    switch(props.pipelineType)
    {
        case GraphicsAPI::D3D11:
        {
            const auto* state = ctrl->GetD3D11PipelineState();
            if(!state)
                break;
            if(stage == "vs")
            {
                info.reflection = state->vertexShader.reflection;
                info.resourceId = state->vertexShader.resourceId;
            }
            else if(stage == "hs")
            {
                info.reflection = state->hullShader.reflection;
                info.resourceId = state->hullShader.resourceId;
            }
            else if(stage == "ds")
            {
                info.reflection = state->domainShader.reflection;
                info.resourceId = state->domainShader.resourceId;
            }
            else if(stage == "gs")
            {
                info.reflection = state->geometryShader.reflection;
                info.resourceId = state->geometryShader.resourceId;
            }
            else if(stage == "ps")
            {
                info.reflection = state->pixelShader.reflection;
                info.resourceId = state->pixelShader.resourceId;
            }
            else if(stage == "cs")
            {
                info.reflection = state->computeShader.reflection;
                info.resourceId = state->computeShader.resourceId;
            }
            break;
        }
        case GraphicsAPI::D3D12:
        {
            const auto* state = ctrl->GetD3D12PipelineState();
            if(!state)
                break;
            if(stage == "vs")
            {
                info.reflection = state->vertexShader.reflection;
                info.resourceId = state->vertexShader.resourceId;
            }
            else if(stage == "hs")
            {
                info.reflection = state->hullShader.reflection;
                info.resourceId = state->hullShader.resourceId;
            }
            else if(stage == "ds")
            {
                info.reflection = state->domainShader.reflection;
                info.resourceId = state->domainShader.resourceId;
            }
            else if(stage == "gs")
            {
                info.reflection = state->geometryShader.reflection;
                info.resourceId = state->geometryShader.resourceId;
            }
            else if(stage == "ps")
            {
                info.reflection = state->pixelShader.reflection;
                info.resourceId = state->pixelShader.resourceId;
            }
            else if(stage == "cs")
            {
                info.reflection = state->computeShader.reflection;
                info.resourceId = state->computeShader.resourceId;
            }
            break;
        }
        case GraphicsAPI::OpenGL:
        {
            const auto* state = ctrl->GetGLPipelineState();
            if(!state)
                break;
            if(stage == "vs")
            {
                info.reflection = state->vertexShader.reflection;
                info.resourceId = state->vertexShader.shaderResourceId;
            }
            else if(stage == "hs")
            {
                info.reflection = state->tessControlShader.reflection;
                info.resourceId = state->tessControlShader.shaderResourceId;
            }
            else if(stage == "ds")
            {
                info.reflection = state->tessEvalShader.reflection;
                info.resourceId = state->tessEvalShader.shaderResourceId;
            }
            else if(stage == "gs")
            {
                info.reflection = state->geometryShader.reflection;
                info.resourceId = state->geometryShader.shaderResourceId;
            }
            else if(stage == "ps")
            {
                info.reflection = state->fragmentShader.reflection;
                info.resourceId = state->fragmentShader.shaderResourceId;
            }
            else if(stage == "cs")
            {
                info.reflection = state->computeShader.reflection;
                info.resourceId = state->computeShader.shaderResourceId;
            }
            break;
        }
        case GraphicsAPI::Vulkan:
        {
            const auto* state = ctrl->GetVulkanPipelineState();
            if(!state)
                break;
            if(stage == "vs")
            {
                info.reflection = state->vertexShader.reflection;
                info.resourceId = state->vertexShader.resourceId;
            }
            else if(stage == "hs")
            {
                info.reflection = state->tessControlShader.reflection;
                info.resourceId = state->tessControlShader.resourceId;
            }
            else if(stage == "ds")
            {
                info.reflection = state->tessEvalShader.reflection;
                info.resourceId = state->tessEvalShader.resourceId;
            }
            else if(stage == "gs")
            {
                info.reflection = state->geometryShader.reflection;
                info.resourceId = state->geometryShader.resourceId;
            }
            else if(stage == "ps")
            {
                info.reflection = state->fragmentShader.reflection;
                info.resourceId = state->fragmentShader.resourceId;
            }
            else if(stage == "cs")
            {
                info.reflection = state->computeShader.reflection;
                info.resourceId = state->computeShader.resourceId;
            }
            break;
        }
        default:
            break;
    }

    return info;
}

// Key for tracking unique shaders: resourceId + stage
struct ShaderKey
{
    uint64_t rawId;
    std::string stage;

    bool operator<(const ShaderKey& o) const
    {
        if(rawId != o.rawId)
            return rawId < o.rawId;
        return stage < o.stage;
    }
};

struct ShaderRecord
{
    ResourceId resourceId;
    std::string stage;
    std::string entryPoint;
    uint32_t usageCount = 0;
    uint32_t firstEventId = 0;    // first event where this shader was seen
};

static uint64_t resourceIdToRaw(ResourceId id)
{
    uint64_t raw = 0;
    memcpy(&raw, &id, sizeof(raw));
    return raw;
}

// Stages to check for draw calls
static const char* kGraphicsStages[] = {"vs", "hs", "ds", "gs", "ps"};
static const char* kAllStages[] = {"vs", "hs", "ds", "gs", "ps", "cs"};

// Collect all actions (draw/dispatch) recursively
static void collectActionEvents(const rdcarray<ActionDescription>& actions,
                                std::vector<uint32_t>& eventIds,
                                int limit)
{
    for(const auto& action : actions)
    {
        if((int)eventIds.size() >= limit)
            return;

        if(bool(action.flags & ActionFlags::Drawcall) ||
           bool(action.flags & ActionFlags::Dispatch))
        {
            eventIds.push_back(action.eventId);
        }

        if(!action.children.empty())
            collectActionEvents(action.children, eventIds, limit);
    }
}

// Collect unique shaders by iterating events
static std::map<ShaderKey, ShaderRecord> collectUniqueShaders(IReplayController* ctrl,
                                                               int maxUniqueShaders)
{
    const auto& rootActions = ctrl->GetRootActions();

    // Collect all draw/dispatch event IDs
    std::vector<uint32_t> eventIds;
    collectActionEvents(rootActions, eventIds, 10000);    // scan up to 10k events

    std::map<ShaderKey, ShaderRecord> shaders;

    for(uint32_t eid : eventIds)
    {
        if((int)shaders.size() >= maxUniqueShaders)
            break;

        ctrl->SetFrameEvent(eid, true);

        for(const char* stageName : kAllStages)
        {
            ShaderStageInfo si = getShaderStageInfo(ctrl, stageName);
            if(!si.reflection || si.resourceId == ResourceId::Null())
                continue;

            uint64_t raw = resourceIdToRaw(si.resourceId);
            ShaderKey key{raw, stageName};

            auto it = shaders.find(key);
            if(it != shaders.end())
            {
                it->second.usageCount++;
            }
            else
            {
                if((int)shaders.size() >= maxUniqueShaders)
                    continue;

                ShaderRecord rec;
                rec.resourceId = si.resourceId;
                rec.stage = stageName;
                rec.entryPoint = std::string(si.reflection->entryPoint.c_str());
                rec.usageCount = 1;
                rec.firstEventId = eid;
                shaders[key] = rec;
            }
        }
    }

    return shaders;
}

void registerShaderTools(ToolRegistry& registry)
{
    // ── get_shader ─────────────────────────────────────────────────────────
    registry.registerTool({
        "get_shader",
        "Get shader disassembly or reflection data at an event for a given stage",
        {
            {"type", "object"},
            {"properties", {
                {"eventId", {{"type", "integer"}, {"description", "Event ID (uses current if omitted)"}}},
                {"stage", {{"type", "string"}, {"enum", json::array({"vs","hs","ds","gs","ps","cs"})}}},
                {"mode", {{"type", "string"}, {"enum", json::array({"disasm","reflect"})}, {"default", "disasm"}}}
            }},
            {"required", json::array({"stage"})}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            std::string stage = args["stage"].get<std::string>();
            std::string mode = args.value("mode", "disasm");

            if(args.contains("eventId"))
            {
                uint32_t eid = args["eventId"].get<uint32_t>();
                ctrl->SetFrameEvent(eid, true);
            }

            ShaderStageInfo si = getShaderStageInfo(ctrl, stage);
            if(!si.reflection)
                throw std::runtime_error("No shader bound at stage '" + stage + "' for the current event.");

            json result;
            result["stage"] = stage;
            result["resourceId"] = resourceIdToString(si.resourceId);
            result["entryPoint"] = std::string(si.reflection->entryPoint.c_str());

            if(mode == "disasm")
            {
                // Get disassembly
                rdcarray<rdcstr> targets = ctrl->GetDisassemblyTargets(true);
                if(targets.isEmpty())
                    throw std::runtime_error("No disassembly targets available.");

                rdcstr disasm = ctrl->DisassembleShader(ResourceId(), si.reflection, targets[0]);
                result["disassembly"] = std::string(disasm.c_str());
                result["target"] = std::string(targets[0].c_str());
            }
            else if(mode == "reflect")
            {
                const ShaderReflection* refl = si.reflection;

                // Input signature
                json inputs = json::array();
                for(int i = 0; i < refl->inputSignature.count(); i++)
                {
                    const auto& sig = refl->inputSignature[i];
                    json s;
                    s["varName"] = std::string(sig.varName.c_str());
                    s["semanticName"] = std::string(sig.semanticName.c_str());
                    s["semanticIndex"] = sig.semanticIndex;
                    s["regIndex"] = sig.regIndex;
                    inputs.push_back(s);
                }
                result["inputSignature"] = inputs;

                // Output signature
                json outputs = json::array();
                for(int i = 0; i < refl->outputSignature.count(); i++)
                {
                    const auto& sig = refl->outputSignature[i];
                    json s;
                    s["varName"] = std::string(sig.varName.c_str());
                    s["semanticName"] = std::string(sig.semanticName.c_str());
                    s["semanticIndex"] = sig.semanticIndex;
                    s["regIndex"] = sig.regIndex;
                    outputs.push_back(s);
                }
                result["outputSignature"] = outputs;

                // Constant blocks
                json cbs = json::array();
                for(int i = 0; i < refl->constantBlocks.count(); i++)
                {
                    const auto& cb = refl->constantBlocks[i];
                    json cbj;
                    cbj["name"] = std::string(cb.name.c_str());
                    cbj["bindPoint"] = cb.fixedBindNumber;
                    cbj["byteSize"] = cb.byteSize;
                    cbj["variableCount"] = (int)cb.variables.count();
                    cbs.push_back(cbj);
                }
                result["constantBlocks"] = cbs;

                // Read-only resources
                json roRes = json::array();
                for(int i = 0; i < refl->readOnlyResources.count(); i++)
                {
                    const auto& r = refl->readOnlyResources[i];
                    json rj;
                    rj["name"] = std::string(r.name.c_str());
                    rj["bindPoint"] = r.fixedBindNumber;
                    roRes.push_back(rj);
                }
                result["readOnlyResources"] = roRes;

                // Read-write resources
                json rwRes = json::array();
                for(int i = 0; i < refl->readWriteResources.count(); i++)
                {
                    const auto& r = refl->readWriteResources[i];
                    json rj;
                    rj["name"] = std::string(r.name.c_str());
                    rj["bindPoint"] = r.fixedBindNumber;
                    rwRes.push_back(rj);
                }
                result["readWriteResources"] = rwRes;
            }

            return result;
        }
    });

    // ── list_shaders ───────────────────────────────────────────────────────
    registry.registerTool({
        "list_shaders",
        "List all unique shaders used in the capture with their stages and usage count",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            auto shaders = collectUniqueShaders(ctrl, 100);

            json shaderList = json::array();
            for(const auto& pair : shaders)
            {
                const ShaderRecord& rec = pair.second;
                json s;
                s["shaderId"] = resourceIdToString(rec.resourceId);
                s["stage"] = rec.stage;
                s["entryPoint"] = rec.entryPoint;
                s["usageCount"] = rec.usageCount;
                shaderList.push_back(s);
            }

            json result;
            result["shaders"] = shaderList;
            result["count"] = shaderList.size();
            return result;
        }
    });

    // ── search_shaders ─────────────────────────────────────────────────────
    registry.registerTool({
        "search_shaders",
        "Search shader disassembly text across all shaders for a pattern",
        {
            {"type", "object"},
            {"properties", {
                {"pattern", {{"type", "string"}, {"description", "Text pattern to search for (case-insensitive substring)"}}},
                {"stage", {{"type", "string"}, {"enum", json::array({"vs","hs","ds","gs","ps","cs"})}, {"description", "Limit to specific stage"}}},
                {"limit", {{"type", "integer"}, {"description", "Max results, default 50"}}}
            }},
            {"required", json::array({"pattern"})}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            std::string pattern = args["pattern"].get<std::string>();
            std::string stageFilter = args.value("stage", "");
            int limit = args.value("limit", 50);

            if(pattern.empty())
                throw std::runtime_error("Search pattern must not be empty.");

            // Lowercase the pattern for case-insensitive search
            std::string lowerPattern = pattern;
            std::transform(lowerPattern.begin(), lowerPattern.end(), lowerPattern.begin(), ::tolower);

            // Collect unique shaders
            auto shaders = collectUniqueShaders(ctrl, 100);

            // Get disassembly targets once
            rdcarray<rdcstr> targets = ctrl->GetDisassemblyTargets(true);
            if(targets.isEmpty())
                throw std::runtime_error("No disassembly targets available.");

            json matches = json::array();
            int matchCount = 0;

            for(const auto& pair : shaders)
            {
                if(matchCount >= limit)
                    break;

                const ShaderRecord& rec = pair.second;

                // Filter by stage if specified
                if(!stageFilter.empty() && rec.stage != stageFilter)
                    continue;

                // Navigate to an event where this shader is used to get reflection
                ctrl->SetFrameEvent(rec.firstEventId, true);
                ShaderStageInfo si = getShaderStageInfo(ctrl, rec.stage);
                if(!si.reflection)
                    continue;

                // Disassemble
                rdcstr disasm = ctrl->DisassembleShader(ResourceId(), si.reflection, targets[0]);
                std::string disasmStr(disasm.c_str());

                // Case-insensitive search
                std::string lowerDisasm = disasmStr;
                std::transform(lowerDisasm.begin(), lowerDisasm.end(), lowerDisasm.begin(), ::tolower);

                size_t pos = lowerDisasm.find(lowerPattern);
                if(pos == std::string::npos)
                    continue;

                // Collect matching lines with context
                json matchEntry;
                matchEntry["shaderId"] = resourceIdToString(rec.resourceId);
                matchEntry["stage"] = rec.stage;
                matchEntry["entryPoint"] = rec.entryPoint;

                // Split disassembly into lines and find matching ones
                json matchingLines = json::array();
                std::istringstream stream(disasmStr);
                std::string line;
                int lineNum = 0;
                int matchedLines = 0;

                while(std::getline(stream, line) && matchedLines < 10)
                {
                    lineNum++;
                    std::string lowerLine = line;
                    std::transform(lowerLine.begin(), lowerLine.end(), lowerLine.begin(), ::tolower);

                    if(lowerLine.find(lowerPattern) != std::string::npos)
                    {
                        json ml;
                        ml["line"] = lineNum;
                        ml["text"] = line;
                        matchingLines.push_back(ml);
                        matchedLines++;
                    }
                }

                matchEntry["matchingLines"] = matchingLines;
                matches.push_back(matchEntry);
                matchCount++;
            }

            json result;
            result["matches"] = matches;
            result["count"] = matches.size();
            result["pattern"] = pattern;
            return result;
        }
    });
}
