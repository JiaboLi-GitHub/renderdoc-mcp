#include "renderdoc_wrapper.h"
#include "renderdoc_replay.h"
#include "stringise.h"

#include <filesystem>
#include <algorithm>
#include <cstring>
#include <sstream>

// Provide template specializations required by renderdoc_tostr.inl macros
template <>
rdcstr DoStringise(const uint32_t &el)
{
  char buf[16];
  snprintf(buf, sizeof(buf), "%u", el);
  return rdcstr(buf);
}

#include "renderdoc_tostr.inl"

namespace fs = std::filesystem;
using json = nlohmann::json;

// ── helpers ─────────────────────────────────────────────────────────────────

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

static std::string actionFlagsToString(ActionFlags flags)
{
    std::string result;
    auto append = [&](const char* name) {
        if(!result.empty()) result += "|";
        result += name;
    };

    if(flags & ActionFlags::Clear) append("Clear");
    if(flags & ActionFlags::Drawcall) append("Drawcall");
    if(flags & ActionFlags::Dispatch) append("Dispatch");
    if(flags & ActionFlags::MeshDispatch) append("MeshDispatch");
    if(flags & ActionFlags::CmdList) append("CmdList");
    if(flags & ActionFlags::SetMarker) append("SetMarker");
    if(flags & ActionFlags::PushMarker) append("PushMarker");
    if(flags & ActionFlags::PopMarker) append("PopMarker");
    if(flags & ActionFlags::Present) append("Present");
    if(flags & ActionFlags::MultiAction) append("MultiAction");
    if(flags & ActionFlags::Copy) append("Copy");
    if(flags & ActionFlags::Resolve) append("Resolve");
    if(flags & ActionFlags::GenMips) append("GenMips");
    if(flags & ActionFlags::PassBoundary) append("PassBoundary");
    if(flags & ActionFlags::DispatchRay) append("DispatchRay");
    if(flags & ActionFlags::BuildAccStruct) append("BuildAccStruct");
    if(flags & ActionFlags::Indexed) append("Indexed");
    if(flags & ActionFlags::Instanced) append("Instanced");
    if(flags & ActionFlags::Indirect) append("Indirect");

    return result.empty() ? "NoFlags" : result;
}

static std::string resourceIdToString(ResourceId id)
{
    if(id == ResourceId::Null())
        return "null";
    // ResourceId is a wrapper around uint64_t with private access.
    // Extract the raw value via memcpy for display purposes.
    uint64_t raw = 0;
    static_assert(sizeof(ResourceId) == sizeof(uint64_t), "ResourceId size mismatch");
    memcpy(&raw, &id, sizeof(raw));
    std::ostringstream oss;
    oss << "ResourceId::" << raw;
    return oss.str();
}

static void flattenActions(const rdcarray<ActionDescription>& actions,
                           const SDFile& structuredFile,
                           const std::string& filter,
                           json& out)
{
    for(const auto& action : actions)
    {
        std::string name = action.GetName(structuredFile).c_str();

        // Apply filter
        if(!filter.empty())
        {
            std::string lowerName = name;
            std::string lowerFilter = filter;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
            bool match = lowerName.find(lowerFilter) != std::string::npos;
            if(!match && !action.children.empty())
            {
                // Still recurse into children
                flattenActions(action.children, structuredFile, filter, out);
                continue;
            }
            if(!match)
                continue;
        }

        json ev;
        ev["eventId"] = action.eventId;
        ev["name"] = name;
        ev["flags"] = actionFlagsToString(action.flags);
        out.push_back(ev);

        // Recurse children
        if(!action.children.empty())
            flattenActions(action.children, structuredFile, filter, out);
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

// ── RenderdocWrapper ────────────────────────────────────────────────────────

RenderdocWrapper::~RenderdocWrapper()
{
    shutdown();
}

void RenderdocWrapper::ensureReplayInitialized()
{
    if(!m_replayInitialized)
    {
        GlobalEnvironment env = {};
        env.enumerateGPUs = true;
        rdcarray<rdcstr> args;
        RENDERDOC_InitialiseReplay(env, args);
        m_replayInitialized = true;
    }
}

void RenderdocWrapper::closeCurrent()
{
    if(m_controller)
    {
        m_controller->Shutdown();
        m_controller = nullptr;
    }
    if(m_captureFile)
    {
        m_captureFile->Shutdown();
        m_captureFile = nullptr;
    }
    m_currentEventId = 0;
    m_capturePath.clear();
}

void RenderdocWrapper::shutdown()
{
    closeCurrent();
    if(m_replayInitialized)
    {
        RENDERDOC_ShutdownReplay();
        m_replayInitialized = false;
    }
}

std::string RenderdocWrapper::getExportDir() const
{
    fs::path capDir = fs::path(m_capturePath).parent_path();
    return (capDir / "renderdoc-mcp-export").string();
}

std::string RenderdocWrapper::generateOutputPath(uint32_t eventId, int index) const
{
    std::string dir = getExportDir();
    fs::create_directories(dir);
    std::ostringstream filename;
    filename << "rt_" << eventId << "_" << index << ".png";
    return (fs::path(dir) / filename.str()).string();
}

// ── Tool implementations ────────────────────────────────────────────────────

json RenderdocWrapper::openCapture(const std::string& path)
{
    ensureReplayInitialized();
    closeCurrent();

    m_captureFile = RENDERDOC_OpenCaptureFile();
    if(!m_captureFile)
        throw std::runtime_error("Failed to create capture file handle");

    ResultDetails result = m_captureFile->OpenFile(rdcstr(path.c_str()), "", nullptr);
    if(!result.OK())
    {
        std::string msg = "Failed to open capture file: " + std::string(result.Message().c_str());
        m_captureFile->Shutdown();
        m_captureFile = nullptr;
        throw std::runtime_error(msg);
    }

    ReplayOptions opts;
    auto [openResult, controller] = m_captureFile->OpenCapture(opts, nullptr);
    if(!openResult.OK())
    {
        std::string msg = "Failed to open capture for replay: " + std::string(openResult.Message().c_str());
        m_captureFile->Shutdown();
        m_captureFile = nullptr;
        throw std::runtime_error(msg);
    }

    m_controller = controller;
    m_capturePath = path;

    APIProperties props = m_controller->GetAPIProperties();
    const auto& actions = m_controller->GetRootActions();
    uint32_t eventCount = countAllEvents(actions);

    json result_json;
    result_json["api"] = graphicsApiToString(props.pipelineType);
    result_json["eventCount"] = eventCount;
    return result_json;
}

json RenderdocWrapper::listEvents(const std::string& filter)
{
    if(!m_controller)
        throw std::runtime_error("No capture is open. Call open_capture first.");

    const auto& actions = m_controller->GetRootActions();
    const SDFile& structuredFile = m_controller->GetStructuredFile();

    json events = json::array();
    flattenActions(actions, structuredFile, filter, events);

    json result;
    result["events"] = events;
    result["count"] = events.size();
    return result;
}

json RenderdocWrapper::gotoEvent(uint32_t eventId)
{
    if(!m_controller)
        throw std::runtime_error("No capture is open. Call open_capture first.");

    m_controller->SetFrameEvent(eventId, true);
    m_currentEventId = eventId;

    json result;
    result["eventId"] = eventId;
    result["status"] = "ok";
    return result;
}

json RenderdocWrapper::getPipelineState()
{
    if(!m_controller)
        throw std::runtime_error("No capture is open. Call open_capture first.");

    APIProperties props = m_controller->GetAPIProperties();
    json result;
    result["api"] = graphicsApiToString(props.pipelineType);
    result["eventId"] = m_currentEventId;

    switch(props.pipelineType)
    {
        case GraphicsAPI::D3D11:
        {
            const D3D11Pipe::State* state = m_controller->GetD3D11PipelineState();
            if(!state) break;

            // Vertex Shader
            result["vertexShader"]["resourceId"] = resourceIdToString(state->vertexShader.resourceId);
            if(state->vertexShader.reflection)
                result["vertexShader"]["entryPoint"] = state->vertexShader.reflection->entryPoint.c_str();

            // Pixel Shader
            result["pixelShader"]["resourceId"] = resourceIdToString(state->pixelShader.resourceId);
            if(state->pixelShader.reflection)
                result["pixelShader"]["entryPoint"] = state->pixelShader.reflection->entryPoint.c_str();

            // Render Targets
            {
                json rts = json::array();
                for(size_t i = 0; i < state->outputMerger.renderTargets.size(); i++)
                {
                    const auto& rt = state->outputMerger.renderTargets[i];
                    if(rt.resource == ResourceId::Null()) continue;
                    json rtj;
                    rtj["index"] = i;
                    rtj["resourceId"] = resourceIdToString(rt.resource);
                    rtj["format"] = rt.format.Name().c_str();
                    rts.push_back(rtj);
                }
                result["renderTargets"] = rts;
            }

            // Viewports
            {
                json vps = json::array();
                for(const auto& vp : state->rasterizer.viewports)
                {
                    if(!vp.enabled) continue;
                    json vpj;
                    vpj["x"] = vp.x;
                    vpj["y"] = vp.y;
                    vpj["width"] = vp.width;
                    vpj["height"] = vp.height;
                    vps.push_back(vpj);
                }
                result["viewports"] = vps;
            }
            break;
        }

        case GraphicsAPI::D3D12:
        {
            const D3D12Pipe::State* state = m_controller->GetD3D12PipelineState();
            if(!state) break;

            result["vertexShader"]["resourceId"] = resourceIdToString(state->vertexShader.resourceId);
            if(state->vertexShader.reflection)
                result["vertexShader"]["entryPoint"] = state->vertexShader.reflection->entryPoint.c_str();

            result["pixelShader"]["resourceId"] = resourceIdToString(state->pixelShader.resourceId);
            if(state->pixelShader.reflection)
                result["pixelShader"]["entryPoint"] = state->pixelShader.reflection->entryPoint.c_str();

            {
                json rts = json::array();
                for(size_t i = 0; i < state->outputMerger.renderTargets.size(); i++)
                {
                    const auto& rt = state->outputMerger.renderTargets[i];
                    if(rt.resource == ResourceId::Null()) continue;
                    json rtj;
                    rtj["index"] = i;
                    rtj["resourceId"] = resourceIdToString(rt.resource);
                    rtj["format"] = rt.format.Name().c_str();
                    rts.push_back(rtj);
                }
                result["renderTargets"] = rts;
            }

            {
                json vps = json::array();
                for(const auto& vp : state->rasterizer.viewports)
                {
                    if(!vp.enabled) continue;
                    json vpj;
                    vpj["x"] = vp.x;
                    vpj["y"] = vp.y;
                    vpj["width"] = vp.width;
                    vpj["height"] = vp.height;
                    vps.push_back(vpj);
                }
                result["viewports"] = vps;
            }
            break;
        }

        case GraphicsAPI::OpenGL:
        {
            const GLPipe::State* state = m_controller->GetGLPipelineState();
            if(!state) break;

            // GL uses shaderResourceId instead of resourceId
            result["vertexShader"]["resourceId"] = resourceIdToString(state->vertexShader.shaderResourceId);
            if(state->vertexShader.reflection)
                result["vertexShader"]["entryPoint"] = state->vertexShader.reflection->entryPoint.c_str();

            result["fragmentShader"]["resourceId"] = resourceIdToString(state->fragmentShader.shaderResourceId);
            if(state->fragmentShader.reflection)
                result["fragmentShader"]["entryPoint"] = state->fragmentShader.reflection->entryPoint.c_str();

            {
                json rts = json::array();
                for(size_t i = 0; i < state->framebuffer.drawFBO.colorAttachments.size(); i++)
                {
                    const auto& att = state->framebuffer.drawFBO.colorAttachments[i];
                    if(att.resource == ResourceId::Null()) continue;
                    json rtj;
                    rtj["index"] = i;
                    rtj["resourceId"] = resourceIdToString(att.resource);
                    rts.push_back(rtj);
                }
                result["renderTargets"] = rts;
            }

            {
                json vps = json::array();
                for(const auto& vp : state->rasterizer.viewports)
                {
                    if(!vp.enabled) continue;
                    json vpj;
                    vpj["x"] = vp.x;
                    vpj["y"] = vp.y;
                    vpj["width"] = vp.width;
                    vpj["height"] = vp.height;
                    vps.push_back(vpj);
                }
                result["viewports"] = vps;
            }
            break;
        }

        case GraphicsAPI::Vulkan:
        {
            const VKPipe::State* state = m_controller->GetVulkanPipelineState();
            if(!state) break;

            result["vertexShader"]["resourceId"] = resourceIdToString(state->vertexShader.resourceId);
            if(state->vertexShader.reflection)
                result["vertexShader"]["entryPoint"] = state->vertexShader.reflection->entryPoint.c_str();

            result["fragmentShader"]["resourceId"] = resourceIdToString(state->fragmentShader.resourceId);
            if(state->fragmentShader.reflection)
                result["fragmentShader"]["entryPoint"] = state->fragmentShader.reflection->entryPoint.c_str();

            {
                json rts = json::array();
                const auto& fb = state->currentPass.framebuffer;
                // colorAttachments is on renderpass, not currentPass directly
                for(size_t i = 0; i < state->currentPass.renderpass.colorAttachments.size(); i++)
                {
                    uint32_t attIdx = state->currentPass.renderpass.colorAttachments[i];
                    if(attIdx < fb.attachments.size())
                    {
                        const auto& att = fb.attachments[attIdx];
                        json rtj;
                        rtj["index"] = i;
                        rtj["resourceId"] = resourceIdToString(att.resource);
                        rtj["format"] = att.format.Name().c_str();
                        rts.push_back(rtj);
                    }
                }
                result["renderTargets"] = rts;
            }

            {
                json vps = json::array();
                for(const auto& vp : state->viewportScissor.viewportScissors)
                {
                    json vpj;
                    vpj["x"] = vp.vp.x;
                    vpj["y"] = vp.vp.y;
                    vpj["width"] = vp.vp.width;
                    vpj["height"] = vp.vp.height;
                    vps.push_back(vpj);
                }
                result["viewports"] = vps;
            }
            break;
        }

        default:
            result["error"] = "Unsupported graphics API";
            break;
    }

    return result;
}

json RenderdocWrapper::exportRenderTarget(int index)
{
    if(!m_controller)
        throw std::runtime_error("No capture is open. Call open_capture first.");

    // Get the RT resource ID from the action's outputs array
    // First, find the current action to get the outputs
    const auto& actions = m_controller->GetRootActions();
    ResourceId rtResourceId;
    bool found = false;

    // Use a lambda to search recursively
    std::function<bool(const rdcarray<ActionDescription>&)> findAction;
    findAction = [&](const rdcarray<ActionDescription>& acts) -> bool {
        for(const auto& action : acts)
        {
            if(action.eventId == m_currentEventId)
            {
                if(index >= 0 && index < 8)
                    rtResourceId = action.outputs[index];
                found = true;
                return true;
            }
            if(!action.children.empty() && findAction(action.children))
                return true;
        }
        return false;
    };
    findAction(actions);

    if(!found)
        throw std::runtime_error("Current event not found in action list. Call goto_event first.");

    if(rtResourceId == ResourceId::Null())
        throw std::runtime_error("No render target bound at index " + std::to_string(index) + " for event " + std::to_string(m_currentEventId));

    // Use SaveTexture
    TextureSave saveData = {};
    saveData.resourceId = rtResourceId;
    saveData.destType = FileType::PNG;
    saveData.alpha = AlphaMapping::Preserve;

    std::string outputPath = generateOutputPath(m_currentEventId, index);

    ResultDetails result = m_controller->SaveTexture(saveData, rdcstr(outputPath.c_str()));
    if(!result.OK())
        throw std::runtime_error("Failed to save render target: " + std::string(result.Message().c_str()));

    // Get texture info for dimensions
    const auto& textures = m_controller->GetTextures();
    uint32_t width = 0, height = 0;
    for(const auto& tex : textures)
    {
        if(tex.resourceId == rtResourceId)
        {
            width = tex.width;
            height = tex.height;
            break;
        }
    }

    json result_json;
    result_json["path"] = outputPath;
    result_json["width"] = width;
    result_json["height"] = height;
    result_json["eventId"] = m_currentEventId;
    result_json["rtIndex"] = index;
    return result_json;
}
