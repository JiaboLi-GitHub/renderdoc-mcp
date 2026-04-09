#include "core/resources.h"
#include "core/errors.h"
#include "core/resource_id.h"
#include "core/session.h"

#include <renderdoc_replay.h>

#include <algorithm>
#include <cctype>

namespace renderdoc::core {

namespace {

std::string resourceTypeToString(ResourceType type) {
    switch (type) {
        case ResourceType::Unknown:       return "Unknown";
        case ResourceType::Device:        return "Device";
        case ResourceType::Queue:         return "Queue";
        case ResourceType::CommandBuffer: return "CommandBuffer";
        case ResourceType::Texture:       return "Texture";
        case ResourceType::Buffer:        return "Buffer";
        case ResourceType::View:          return "View";
        case ResourceType::Sampler:       return "Sampler";
        case ResourceType::SwapchainImage: return "SwapchainImage";
        case ResourceType::Memory:        return "Memory";
        case ResourceType::Shader:        return "Shader";
        case ResourceType::ShaderBinding: return "ShaderBinding";
        case ResourceType::PipelineState: return "PipelineState";
        case ResourceType::StateObject:   return "StateObject";
        case ResourceType::RenderPass:    return "RenderPass";
        case ResourceType::Query:         return "Query";
        case ResourceType::Sync:          return "Sync";
        case ResourceType::Pool:          return "Pool";
        default:                          return "Other";
    }
}

std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

// Populate texture-specific fields on a ResourceInfo.
void fillTextureFields(ResourceInfo& info, const TextureDescription& tex) {
    info.byteSize = tex.byteSize;
    info.format = std::string(tex.format.Name().c_str());
    info.width = tex.width;
    info.height = tex.height;
    info.depth = tex.depth;
    info.mips = tex.mips;
    info.arraySize = tex.arraysize;
    info.cubemap = tex.cubemap;
    info.msSamp = tex.msSamp;
    info.dimension = std::to_string(tex.dimension);

    ResourceInfo::FormatDetails fmt;
    fmt.name = std::string(tex.format.Name().c_str());
    fmt.compCount = tex.format.compCount;
    fmt.compByteWidth = tex.format.compByteWidth;
    fmt.compType = static_cast<uint32_t>(tex.format.compType);
    info.formatDetails = std::move(fmt);
}

// Populate buffer-specific fields on a ResourceInfo.
void fillBufferFields(ResourceInfo& info, const BufferDescription& buf) {
    info.byteSize = buf.length;
    info.gpuAddress = buf.gpuAddress;
}

// Count draws and dispatches recursively.
void countDrawsAndDispatches(const rdcarray<ActionDescription>& actions,
                              uint32_t& drawCount, uint32_t& dispatchCount) {
    for (const auto& action : actions) {
        if (bool(action.flags & ActionFlags::Drawcall))
            drawCount++;
        if (bool(action.flags & ActionFlags::Dispatch))
            dispatchCount++;
        if (!action.children.empty())
            countDrawsAndDispatches(action.children, drawCount, dispatchCount);
    }
}

bool hasDrawCalls(const rdcarray<ActionDescription>& actions) {
    for (const auto& action : actions) {
        if (bool(action.flags & ActionFlags::Drawcall) || bool(action.flags & ActionFlags::Dispatch))
            return true;
        if (!action.children.empty() && hasDrawCalls(action.children))
            return true;
    }
    return false;
}

const ActionDescription* findActionByEventId(const rdcarray<ActionDescription>& actions,
                                              uint32_t eventId) {
    for (const auto& action : actions) {
        if (action.eventId == eventId)
            return &action;
        if (!action.children.empty()) {
            const ActionDescription* found = findActionByEventId(action.children, eventId);
            if (found)
                return found;
        }
    }
    return nullptr;
}

} // anonymous namespace

// ---------------------------------------------------------------------------

std::vector<ResourceInfo> listResources(const Session& session,
                                         const std::string& typeFilter,
                                         const std::string& nameFilter) {
    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open

    const std::string typeFilterLower = toLower(typeFilter);
    const std::string nameFilterLower = toLower(nameFilter);

    const auto& textures = ctrl->GetTextures();
    const auto& buffers  = ctrl->GetBuffers();
    rdcarray<ResourceDescription> resDescs = ctrl->GetResources();

    std::vector<ResourceInfo> result;

    for (const auto& res : resDescs) {
        std::string typeName = resourceTypeToString(res.type);

        if (!typeFilter.empty()) {
            if (toLower(typeName).find(typeFilterLower) == std::string::npos)
                continue;
        }

        std::string name(res.name.c_str());
        if (!nameFilter.empty()) {
            if (toLower(name).find(nameFilterLower) == std::string::npos)
                continue;
        }

        ResourceInfo info;
        info.id   = toResourceId(res.resourceId);
        info.name = name;
        info.type = typeName;

        if (res.type == ResourceType::Texture || res.type == ResourceType::SwapchainImage) {
            for (const auto& tex : textures) {
                if (tex.resourceId == res.resourceId) {
                    fillTextureFields(info, tex);
                    break;
                }
            }
        } else if (res.type == ResourceType::Buffer) {
            for (const auto& buf : buffers) {
                if (buf.resourceId == res.resourceId) {
                    fillBufferFields(info, buf);
                    break;
                }
            }
        }

        result.push_back(std::move(info));
    }

    return result;
}

// ---------------------------------------------------------------------------

ResourceInfo getResourceDetails(const Session& session, ResourceId id) {
    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open

    ::ResourceId targetId = fromResourceId(id);

    rdcarray<ResourceDescription> resDescs = ctrl->GetResources();
    const ResourceDescription* found = nullptr;
    for (const auto& res : resDescs) {
        if (res.resourceId == targetId) {
            found = &res;
            break;
        }
    }

    if (!found)
        throw CoreError(CoreError::Code::InvalidResourceId,
                        "Resource not found: " + std::to_string(id));

    ResourceInfo info;
    info.id   = toResourceId(found->resourceId);
    info.name = std::string(found->name.c_str());
    info.type = resourceTypeToString(found->type);

    if (found->type == ResourceType::Texture || found->type == ResourceType::SwapchainImage) {
        const auto& textures = ctrl->GetTextures();
        for (const auto& tex : textures) {
            if (tex.resourceId == targetId) {
                fillTextureFields(info, tex);
                break;
            }
        }
    } else if (found->type == ResourceType::Buffer) {
        const auto& buffers = ctrl->GetBuffers();
        for (const auto& buf : buffers) {
            if (buf.resourceId == targetId) {
                fillBufferFields(info, buf);
                break;
            }
        }
    }

    return info;
}

// ---------------------------------------------------------------------------

std::vector<PassInfo> listPasses(const Session& session) {
    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open

    const auto& rootActions   = ctrl->GetRootActions();
    const SDFile& structuredFile = ctrl->GetStructuredFile();

    std::vector<PassInfo> result;

    for (const auto& action : rootActions) {
        // A pass is a top-level action with children that contain draw/dispatch calls.
        if (action.children.empty())
            continue;
        if (!hasDrawCalls(action.children))
            continue;

        PassInfo pass;
        pass.name    = std::string(action.GetName(structuredFile).c_str());
        pass.eventId = action.eventId;
        countDrawsAndDispatches(action.children, pass.drawCount, pass.dispatchCount);

        result.push_back(std::move(pass));
    }

    return result;
}

// ---------------------------------------------------------------------------

PassInfo getPassInfo(const Session& session, uint32_t eventId) {
    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open

    const auto& rootActions   = ctrl->GetRootActions();
    const SDFile& structuredFile = ctrl->GetStructuredFile();

    const ActionDescription* passAction = findActionByEventId(rootActions, eventId);
    if (!passAction)
        throw CoreError(CoreError::Code::InvalidEventId,
                        "Event ID " + std::to_string(eventId) + " not found.");

    PassInfo pass;
    pass.name    = std::string(passAction->GetName(structuredFile).c_str());
    pass.eventId = passAction->eventId;

    for (const auto& child : passAction->children) {
        if (bool(child.flags & ActionFlags::Drawcall) || bool(child.flags & ActionFlags::Dispatch)) {
            EventInfo draw;
            draw.eventId      = child.eventId;
            draw.name         = std::string(child.GetName(structuredFile).c_str());
            draw.flags        = static_cast<ActionFlagBits>(child.flags);
            draw.numIndices   = child.numIndices;
            draw.numInstances = child.numInstances;
            draw.drawIndex    = child.drawIndex;

            if (bool(child.flags & ActionFlags::Drawcall))
                pass.drawCount++;
            else
                pass.dispatchCount++;

            pass.draws.push_back(std::move(draw));
        }
    }

    return pass;
}

} // namespace renderdoc::core
