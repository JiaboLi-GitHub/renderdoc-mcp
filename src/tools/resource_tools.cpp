#include "tools.h"
#include "../tool_registry.h"
#include "../renderdoc_wrapper.h"
#include "renderdoc_replay.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>

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

static ResourceId parseResourceId(const std::string& str)
{
    uint64_t raw = 0;
    size_t pos = str.find("::");
    if(pos != std::string::npos)
        raw = std::stoull(str.substr(pos + 2));
    else
        raw = std::stoull(str);
    ResourceId id;
    memcpy(&id, &raw, sizeof(raw));
    return id;
}

static void countDrawsAndDispatches(const rdcarray<ActionDescription>& actions,
                                    int& drawCount, int& dispatchCount)
{
    for(const auto& action : actions)
    {
        if(bool(action.flags & ActionFlags::Drawcall))
            drawCount++;
        if(bool(action.flags & ActionFlags::Dispatch))
            dispatchCount++;
        if(!action.children.empty())
            countDrawsAndDispatches(action.children, drawCount, dispatchCount);
    }
}

static bool hasDrawCalls(const rdcarray<ActionDescription>& actions)
{
    for(const auto& action : actions)
    {
        if(bool(action.flags & ActionFlags::Drawcall) || bool(action.flags & ActionFlags::Dispatch))
            return true;
        if(!action.children.empty() && hasDrawCalls(action.children))
            return true;
    }
    return false;
}

static const ActionDescription* findActionByEventId(const rdcarray<ActionDescription>& actions,
                                                     uint32_t eventId)
{
    for(const auto& action : actions)
    {
        if(action.eventId == eventId)
            return &action;
        if(!action.children.empty())
        {
            const ActionDescription* found = findActionByEventId(action.children, eventId);
            if(found)
                return found;
        }
    }
    return nullptr;
}

static std::string resourceTypeToString(ResourceType type)
{
    switch(type)
    {
        case ResourceType::Unknown: return "Unknown";
        case ResourceType::Device: return "Device";
        case ResourceType::Queue: return "Queue";
        case ResourceType::CommandBuffer: return "CommandBuffer";
        case ResourceType::Texture: return "Texture";
        case ResourceType::Buffer: return "Buffer";
        case ResourceType::View: return "View";
        case ResourceType::Sampler: return "Sampler";
        case ResourceType::SwapchainImage: return "SwapchainImage";
        case ResourceType::Memory: return "Memory";
        case ResourceType::Shader: return "Shader";
        case ResourceType::ShaderBinding: return "ShaderBinding";
        case ResourceType::PipelineState: return "PipelineState";
        case ResourceType::StateObject: return "StateObject";
        case ResourceType::RenderPass: return "RenderPass";
        case ResourceType::Query: return "Query";
        case ResourceType::Sync: return "Sync";
        case ResourceType::Pool: return "Pool";
        default: return "Other";
    }
}

void registerResourceTools(ToolRegistry& registry)
{
    // list_resources
    registry.registerTool({
        "list_resources",
        "List all GPU resources (textures, buffers) in the capture with size and format info",
        {
            {"type", "object"},
            {"properties", {
                {"type", {{"type", "string"}, {"description", "Filter by type: Texture, Buffer, Shader, etc."}}},
                {"name", {{"type", "string"}, {"description", "Filter by name keyword"}}}
            }}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            std::string typeFilter = args.value("type", "");
            std::string nameFilter = args.value("name", "");

            std::string typeFilterLower = typeFilter;
            std::transform(typeFilterLower.begin(), typeFilterLower.end(), typeFilterLower.begin(), ::tolower);
            std::string nameFilterLower = nameFilter;
            std::transform(nameFilterLower.begin(), nameFilterLower.end(), nameFilterLower.begin(), ::tolower);

            const auto& textures = ctrl->GetTextures();
            const auto& buffers = ctrl->GetBuffers();
            rdcarray<ResourceDescription> resources = ctrl->GetResources();

            json items = json::array();

            for(const auto& res : resources)
            {
                std::string typeName = resourceTypeToString(res.type);

                if(!typeFilter.empty())
                {
                    std::string typeNameLower = typeName;
                    std::transform(typeNameLower.begin(), typeNameLower.end(), typeNameLower.begin(), ::tolower);
                    if(typeNameLower.find(typeFilterLower) == std::string::npos)
                        continue;
                }

                std::string name(res.name.c_str());
                if(!nameFilter.empty())
                {
                    std::string nameLower = name;
                    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                    if(nameLower.find(nameFilterLower) == std::string::npos)
                        continue;
                }

                json entry;
                entry["resourceId"] = resourceIdToString(res.resourceId);
                entry["name"] = name;
                entry["type"] = typeName;

                if(res.type == ResourceType::Texture || res.type == ResourceType::SwapchainImage)
                {
                    for(const auto& tex : textures)
                    {
                        if(tex.resourceId == res.resourceId)
                        {
                            entry["format"] = std::string(tex.format.Name().c_str());
                            entry["width"] = tex.width;
                            entry["height"] = tex.height;
                            entry["depth"] = tex.depth;
                            entry["byteSize"] = tex.byteSize;
                            break;
                        }
                    }
                }
                else if(res.type == ResourceType::Buffer)
                {
                    for(const auto& buf : buffers)
                    {
                        if(buf.resourceId == res.resourceId)
                        {
                            entry["length"] = buf.length;
                            break;
                        }
                    }
                }

                items.push_back(entry);
            }

            json result;
            result["resources"] = items;
            result["count"] = items.size();
            return result;
        }
    });

    // get_resource_info
    registry.registerTool({
        "get_resource_info",
        "Get detailed information about a specific GPU resource by its ID",
        {
            {"type", "object"},
            {"properties", {
                {"resourceId", {{"type", "string"}, {"description", "Resource ID string (e.g. ResourceId::123)"}}}
            }},
            {"required", json::array({"resourceId"})}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            std::string idStr = args["resourceId"].get<std::string>();
            ResourceId targetId = parseResourceId(idStr);

            rdcarray<ResourceDescription> resources = ctrl->GetResources();
            const ResourceDescription* found = nullptr;
            for(const auto& res : resources)
            {
                if(res.resourceId == targetId)
                {
                    found = &res;
                    break;
                }
            }

            if(!found)
                throw std::runtime_error("Resource not found: " + idStr);

            json result;
            result["resourceId"] = resourceIdToString(found->resourceId);
            result["name"] = std::string(found->name.c_str());
            result["type"] = resourceTypeToString(found->type);

            if(found->type == ResourceType::Texture || found->type == ResourceType::SwapchainImage)
            {
                const auto& textures = ctrl->GetTextures();
                for(const auto& tex : textures)
                {
                    if(tex.resourceId == targetId)
                    {
                        result["format"] = std::string(tex.format.Name().c_str());
                        result["width"] = tex.width;
                        result["height"] = tex.height;
                        result["depth"] = tex.depth;
                        result["mips"] = tex.mips;
                        result["arraysize"] = tex.arraysize;
                        result["byteSize"] = tex.byteSize;
                        result["dimension"] = tex.dimension;
                        result["cubemap"] = tex.cubemap;
                        result["msSamp"] = tex.msSamp;

                        json fmt;
                        fmt["name"] = std::string(tex.format.Name().c_str());
                        fmt["compCount"] = tex.format.compCount;
                        fmt["compByteWidth"] = tex.format.compByteWidth;
                        fmt["compType"] = (uint32_t)tex.format.compType;
                        result["formatDetails"] = fmt;
                        break;
                    }
                }
            }
            else if(found->type == ResourceType::Buffer)
            {
                const auto& buffers = ctrl->GetBuffers();
                for(const auto& buf : buffers)
                {
                    if(buf.resourceId == targetId)
                    {
                        result["length"] = buf.length;
                        result["gpuAddress"] = buf.gpuAddress;
                        break;
                    }
                }
            }

            return result;
        }
    });

    // list_passes
    registry.registerTool({
        "list_passes",
        "List all render passes in the capture (marker regions with draw calls)",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            (void)args;
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            const auto& rootActions = ctrl->GetRootActions();
            const SDFile& structuredFile = ctrl->GetStructuredFile();

            json passes = json::array();

            for(const auto& action : rootActions)
            {
                // A pass is a top-level action that has children and contains draw/dispatch calls
                if(action.children.empty())
                    continue;
                if(!hasDrawCalls(action.children))
                    continue;

                int drawCount = 0;
                int dispatchCount = 0;
                countDrawsAndDispatches(action.children, drawCount, dispatchCount);

                json pass;
                pass["name"] = std::string(action.GetName(structuredFile).c_str());
                pass["eventId"] = action.eventId;
                pass["drawCount"] = drawCount;
                pass["dispatchCount"] = dispatchCount;
                passes.push_back(pass);
            }

            json result;
            result["passes"] = passes;
            result["count"] = passes.size();
            return result;
        }
    });

    // get_pass_info
    registry.registerTool({
        "get_pass_info",
        "Get details about a specific render pass including its draw calls",
        {
            {"type", "object"},
            {"properties", {
                {"eventId", {{"type", "integer"}, {"description", "Event ID of the pass marker"}}}
            }},
            {"required", json::array({"eventId"})}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            uint32_t eventId = args["eventId"].get<uint32_t>();
            const auto& rootActions = ctrl->GetRootActions();
            const SDFile& structuredFile = ctrl->GetStructuredFile();

            const ActionDescription* passAction = findActionByEventId(rootActions, eventId);
            if(!passAction)
                throw std::runtime_error("Event ID " + std::to_string(eventId) + " not found.");

            json draws = json::array();
            int drawCount = 0;
            int dispatchCount = 0;

            for(const auto& child : passAction->children)
            {
                if(bool(child.flags & ActionFlags::Drawcall))
                {
                    json draw;
                    draw["eventId"] = child.eventId;
                    draw["name"] = std::string(child.GetName(structuredFile).c_str());
                    draw["numIndices"] = child.numIndices;
                    draw["numInstances"] = child.numInstances;
                    draws.push_back(draw);
                    drawCount++;
                }
                else if(bool(child.flags & ActionFlags::Dispatch))
                {
                    json draw;
                    draw["eventId"] = child.eventId;
                    draw["name"] = std::string(child.GetName(structuredFile).c_str());
                    draw["numIndices"] = child.numIndices;
                    draw["numInstances"] = child.numInstances;
                    draws.push_back(draw);
                    dispatchCount++;
                }
            }

            json result;
            result["name"] = std::string(passAction->GetName(structuredFile).c_str());
            result["eventId"] = passAction->eventId;
            result["draws"] = draws;
            result["drawCount"] = drawCount;
            result["dispatchCount"] = dispatchCount;
            return result;
        }
    });
}
