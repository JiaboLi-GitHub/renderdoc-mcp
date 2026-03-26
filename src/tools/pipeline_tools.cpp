#include "tools.h"
#include "../tool_registry.h"
#include "../renderdoc_wrapper.h"
#include "renderdoc_replay.h"

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

// Helper: extract shader bindings from reflection data
static json extractShaderBindings(const ShaderReflection* refl)
{
    json bindings;
    if(!refl)
        return bindings;

    // Constant blocks
    if(refl->constantBlocks.count() > 0)
    {
        json cbs = json::array();
        for(int i = 0; i < refl->constantBlocks.count(); i++)
        {
            const auto& cb = refl->constantBlocks[i];
            json cbj;
            cbj["name"] = std::string(cb.name.c_str());
            cbj["bindPoint"] = cb.fixedBindNumber;
            cbj["byteSize"] = cb.byteSize;
            cbj["variables"] = (int)cb.variables.count();
            cbs.push_back(cbj);
        }
        bindings["constantBuffers"] = cbs;
    }

    // Read-only resources (SRVs/textures)
    if(refl->readOnlyResources.count() > 0)
    {
        json srvs = json::array();
        for(int i = 0; i < refl->readOnlyResources.count(); i++)
        {
            const auto& srv = refl->readOnlyResources[i];
            json srvj;
            srvj["name"] = std::string(srv.name.c_str());
            srvj["bindPoint"] = srv.fixedBindNumber;
            srvs.push_back(srvj);
        }
        bindings["readOnlyResources"] = srvs;
    }

    // Read-write resources (UAVs)
    if(refl->readWriteResources.count() > 0)
    {
        json uavs = json::array();
        for(int i = 0; i < refl->readWriteResources.count(); i++)
        {
            const auto& uav = refl->readWriteResources[i];
            json uavj;
            uavj["name"] = std::string(uav.name.c_str());
            uavj["bindPoint"] = uav.fixedBindNumber;
            uavs.push_back(uavj);
        }
        bindings["readWriteResources"] = uavs;
    }

    // Samplers
    if(refl->samplers.count() > 0)
    {
        json samps = json::array();
        for(int i = 0; i < refl->samplers.count(); i++)
        {
            const auto& samp = refl->samplers[i];
            json sampj;
            sampj["name"] = std::string(samp.name.c_str());
            sampj["bindPoint"] = samp.fixedBindNumber;
            samps.push_back(sampj);
        }
        bindings["samplers"] = samps;
    }

    return bindings;
}

void registerPipelineTools(ToolRegistry& registry)
{
    // get_pipeline_state (enhanced: optional eventId param)
    registry.registerTool({
        "get_pipeline_state",
        "Get the graphics pipeline state at the current or specified event. Returns bound shaders, render targets, viewports, and pipeline configuration.",
        {
            {"type", "object"},
            {"properties", {
                {"eventId", {{"type", "integer"}, {"description", "Event ID to inspect (uses current if omitted)"}}}
            }}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            if(args.contains("eventId"))
            {
                uint32_t eid = args["eventId"].get<uint32_t>();
                ctrl->SetFrameEvent(eid, true);
            }

            return w.getPipelineState();
        }
    });

    // get_bindings - uses per-API reflection data
    registry.registerTool({
        "get_bindings",
        "Get descriptor/resource bindings for all shader stages at the current or specified event. Shows constant buffers, textures, UAVs, and samplers from shader reflection.",
        {
            {"type", "object"},
            {"properties", {
                {"eventId", {{"type", "integer"}, {"description", "Event ID (uses current if omitted)"}}}
            }}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            if(args.contains("eventId"))
            {
                uint32_t eid = args["eventId"].get<uint32_t>();
                ctrl->SetFrameEvent(eid, true);
            }

            APIProperties props = ctrl->GetAPIProperties();
            json result;
            result["api"] = graphicsApiToString(props.pipelineType);
            result["eventId"] = w.getCurrentEventId();
            json stages = json::object();

            switch(props.pipelineType)
            {
                case GraphicsAPI::D3D11:
                {
                    const auto* state = ctrl->GetD3D11PipelineState();
                    if(!state) break;
                    if(state->vertexShader.reflection)
                    {
                        json vs;
                        vs["shader"] = resourceIdToString(state->vertexShader.resourceId);
                        vs["bindings"] = extractShaderBindings(state->vertexShader.reflection);
                        stages["vs"] = vs;
                    }
                    if(state->pixelShader.reflection)
                    {
                        json ps;
                        ps["shader"] = resourceIdToString(state->pixelShader.resourceId);
                        ps["bindings"] = extractShaderBindings(state->pixelShader.reflection);
                        stages["ps"] = ps;
                    }
                    if(state->hullShader.reflection)
                    {
                        json hs;
                        hs["shader"] = resourceIdToString(state->hullShader.resourceId);
                        hs["bindings"] = extractShaderBindings(state->hullShader.reflection);
                        stages["hs"] = hs;
                    }
                    if(state->domainShader.reflection)
                    {
                        json ds;
                        ds["shader"] = resourceIdToString(state->domainShader.resourceId);
                        ds["bindings"] = extractShaderBindings(state->domainShader.reflection);
                        stages["ds"] = ds;
                    }
                    if(state->geometryShader.reflection)
                    {
                        json gs;
                        gs["shader"] = resourceIdToString(state->geometryShader.resourceId);
                        gs["bindings"] = extractShaderBindings(state->geometryShader.reflection);
                        stages["gs"] = gs;
                    }
                    if(state->computeShader.reflection)
                    {
                        json cs;
                        cs["shader"] = resourceIdToString(state->computeShader.resourceId);
                        cs["bindings"] = extractShaderBindings(state->computeShader.reflection);
                        stages["cs"] = cs;
                    }
                    break;
                }
                case GraphicsAPI::D3D12:
                {
                    const auto* state = ctrl->GetD3D12PipelineState();
                    if(!state) break;
                    if(state->vertexShader.reflection)
                    {
                        json vs;
                        vs["shader"] = resourceIdToString(state->vertexShader.resourceId);
                        vs["bindings"] = extractShaderBindings(state->vertexShader.reflection);
                        stages["vs"] = vs;
                    }
                    if(state->pixelShader.reflection)
                    {
                        json ps;
                        ps["shader"] = resourceIdToString(state->pixelShader.resourceId);
                        ps["bindings"] = extractShaderBindings(state->pixelShader.reflection);
                        stages["ps"] = ps;
                    }
                    if(state->hullShader.reflection)
                    {
                        json hs;
                        hs["shader"] = resourceIdToString(state->hullShader.resourceId);
                        hs["bindings"] = extractShaderBindings(state->hullShader.reflection);
                        stages["hs"] = hs;
                    }
                    if(state->domainShader.reflection)
                    {
                        json ds;
                        ds["shader"] = resourceIdToString(state->domainShader.resourceId);
                        ds["bindings"] = extractShaderBindings(state->domainShader.reflection);
                        stages["ds"] = ds;
                    }
                    if(state->geometryShader.reflection)
                    {
                        json gs;
                        gs["shader"] = resourceIdToString(state->geometryShader.resourceId);
                        gs["bindings"] = extractShaderBindings(state->geometryShader.reflection);
                        stages["gs"] = gs;
                    }
                    if(state->computeShader.reflection)
                    {
                        json cs;
                        cs["shader"] = resourceIdToString(state->computeShader.resourceId);
                        cs["bindings"] = extractShaderBindings(state->computeShader.reflection);
                        stages["cs"] = cs;
                    }
                    break;
                }
                case GraphicsAPI::OpenGL:
                {
                    const auto* state = ctrl->GetGLPipelineState();
                    if(!state) break;
                    if(state->vertexShader.reflection)
                    {
                        json vs;
                        vs["shader"] = resourceIdToString(state->vertexShader.shaderResourceId);
                        vs["bindings"] = extractShaderBindings(state->vertexShader.reflection);
                        stages["vs"] = vs;
                    }
                    if(state->fragmentShader.reflection)
                    {
                        json ps;
                        ps["shader"] = resourceIdToString(state->fragmentShader.shaderResourceId);
                        ps["bindings"] = extractShaderBindings(state->fragmentShader.reflection);
                        stages["ps"] = ps;
                    }
                    if(state->tessEvalShader.reflection)
                    {
                        json ds;
                        ds["shader"] = resourceIdToString(state->tessEvalShader.shaderResourceId);
                        ds["bindings"] = extractShaderBindings(state->tessEvalShader.reflection);
                        stages["ds"] = ds;
                    }
                    if(state->tessControlShader.reflection)
                    {
                        json hs;
                        hs["shader"] = resourceIdToString(state->tessControlShader.shaderResourceId);
                        hs["bindings"] = extractShaderBindings(state->tessControlShader.reflection);
                        stages["hs"] = hs;
                    }
                    if(state->geometryShader.reflection)
                    {
                        json gs;
                        gs["shader"] = resourceIdToString(state->geometryShader.shaderResourceId);
                        gs["bindings"] = extractShaderBindings(state->geometryShader.reflection);
                        stages["gs"] = gs;
                    }
                    if(state->computeShader.reflection)
                    {
                        json cs;
                        cs["shader"] = resourceIdToString(state->computeShader.shaderResourceId);
                        cs["bindings"] = extractShaderBindings(state->computeShader.reflection);
                        stages["cs"] = cs;
                    }
                    break;
                }
                case GraphicsAPI::Vulkan:
                {
                    const auto* state = ctrl->GetVulkanPipelineState();
                    if(!state) break;
                    if(state->vertexShader.reflection)
                    {
                        json vs;
                        vs["shader"] = resourceIdToString(state->vertexShader.resourceId);
                        vs["bindings"] = extractShaderBindings(state->vertexShader.reflection);
                        stages["vs"] = vs;
                    }
                    if(state->fragmentShader.reflection)
                    {
                        json ps;
                        ps["shader"] = resourceIdToString(state->fragmentShader.resourceId);
                        ps["bindings"] = extractShaderBindings(state->fragmentShader.reflection);
                        stages["ps"] = ps;
                    }
                    if(state->tessEvalShader.reflection)
                    {
                        json ds;
                        ds["shader"] = resourceIdToString(state->tessEvalShader.resourceId);
                        ds["bindings"] = extractShaderBindings(state->tessEvalShader.reflection);
                        stages["ds"] = ds;
                    }
                    if(state->tessControlShader.reflection)
                    {
                        json hs;
                        hs["shader"] = resourceIdToString(state->tessControlShader.resourceId);
                        hs["bindings"] = extractShaderBindings(state->tessControlShader.reflection);
                        stages["hs"] = hs;
                    }
                    if(state->geometryShader.reflection)
                    {
                        json gs;
                        gs["shader"] = resourceIdToString(state->geometryShader.resourceId);
                        gs["bindings"] = extractShaderBindings(state->geometryShader.reflection);
                        stages["gs"] = gs;
                    }
                    if(state->computeShader.reflection)
                    {
                        json cs;
                        cs["shader"] = resourceIdToString(state->computeShader.resourceId);
                        cs["bindings"] = extractShaderBindings(state->computeShader.reflection);
                        stages["cs"] = cs;
                    }
                    break;
                }
                default:
                    break;
            }

            result["stages"] = stages;
            return result;
        }
    });
}
