#include "core/action_helpers.h"

namespace renderdoc::core {

GraphicsApi toGraphicsApi(GraphicsAPI api) {
    switch (api) {
        case GraphicsAPI::D3D11:  return GraphicsApi::D3D11;
        case GraphicsAPI::D3D12:  return GraphicsApi::D3D12;
        case GraphicsAPI::OpenGL: return GraphicsApi::OpenGL;
        case GraphicsAPI::Vulkan: return GraphicsApi::Vulkan;
        default:                  return GraphicsApi::Unknown;
    }
}

uint32_t countAllEvents(const ActionDescription& action) {
    uint32_t count = 1;
    for (const auto& child : action.children)
        count += countAllEvents(child);
    return count;
}

uint32_t countDrawCalls(const ActionDescription& action) {
    uint32_t count = 0;
    if (action.flags & ActionFlags::Drawcall)
        count = 1;
    for (const auto& child : action.children)
        count += countDrawCalls(child);
    return count;
}

uint32_t countAllEvents(const rdcarray<ActionDescription>& actions) {
    uint32_t count = 0;
    for (const auto& action : actions)
        count += countAllEvents(action);
    return count;
}

uint32_t countDrawCalls(const rdcarray<ActionDescription>& actions) {
    uint32_t count = 0;
    for (const auto& action : actions)
        count += countDrawCalls(action);
    return count;
}

} // namespace renderdoc::core
