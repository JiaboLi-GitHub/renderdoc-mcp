#include "tools.h"
#include "../tool_registry.h"
#include "../renderdoc_wrapper.h"
#include "renderdoc_replay.h"
#include <algorithm>
#include <cstring>
#include <sstream>

using json = nlohmann::json;

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
    uint64_t raw = 0;
    static_assert(sizeof(ResourceId) == sizeof(uint64_t), "ResourceId size mismatch");
    memcpy(&raw, &id, sizeof(raw));
    std::ostringstream oss;
    oss << "ResourceId::" << raw;
    return oss.str();
}

static void collectDrawCalls(const rdcarray<ActionDescription>& actions,
                             const SDFile& structuredFile,
                             const std::string& filter,
                             int limit,
                             json& out)
{
    for(const auto& action : actions)
    {
        if(out.size() >= static_cast<size_t>(limit))
            return;

        if(bool(action.flags & ActionFlags::Drawcall))
        {
            std::string name = action.GetName(structuredFile).c_str();

            // Apply optional name filter (case-insensitive)
            if(!filter.empty())
            {
                std::string lowerName = name;
                std::string lowerFilter = filter;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
                if(lowerName.find(lowerFilter) == std::string::npos)
                {
                    // Still recurse into children
                    if(!action.children.empty())
                        collectDrawCalls(action.children, structuredFile, filter, limit, out);
                    continue;
                }
            }

            json draw;
            draw["eventId"] = action.eventId;
            draw["name"] = name;
            draw["flags"] = actionFlagsToString(action.flags);
            draw["numIndices"] = action.numIndices;
            draw["numInstances"] = action.numInstances;
            draw["drawIndex"] = action.drawIndex;
            out.push_back(draw);
        }

        if(!action.children.empty())
            collectDrawCalls(action.children, structuredFile, filter, limit, out);
    }
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

void registerEventTools(ToolRegistry& registry)
{
    // list_events
    registry.registerTool({
        "list_events",
        "List all draw calls and actions in the currently opened capture. Returns event IDs, names, and action flags.",
        {
            {"type", "object"},
            {"properties", {
                {"filter", {{"type", "string"}, {"description", "Optional case-insensitive filter keyword to match event names"}}}
            }}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            std::string filter = args.value("filter", "");
            return w.listEvents(filter);
        }
    });

    // goto_event
    registry.registerTool({
        "goto_event",
        "Navigate to a specific event by its event ID. This sets the replay position so that subsequent pipeline state queries and render target exports reflect this event.",
        {
            {"type", "object"},
            {"properties", {
                {"eventId", {{"type", "integer"}, {"description", "The event ID to navigate to"}}}
            }},
            {"required", json::array({"eventId"})}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            return w.gotoEvent(args["eventId"].get<uint32_t>());
        }
    });

    // list_draws
    registry.registerTool({
        "list_draws",
        "List draw calls with vertex/index counts, instance counts, and flags",
        {
            {"type", "object"},
            {"properties", {
                {"filter", {{"type", "string"}, {"description", "Filter by name keyword"}}},
                {"limit", {{"type", "integer"}, {"description", "Max results, default 1000"}}}
            }}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            std::string filter = args.value("filter", "");
            int limit = args.value("limit", 1000);

            const auto& actions = ctrl->GetRootActions();
            const SDFile& structuredFile = ctrl->GetStructuredFile();

            json draws = json::array();
            collectDrawCalls(actions, structuredFile, filter, limit, draws);

            json result;
            result["draws"] = draws;
            result["count"] = draws.size();
            return result;
        }
    });

    // get_draw_info
    registry.registerTool({
        "get_draw_info",
        "Get detailed information about a specific draw call",
        {
            {"type", "object"},
            {"properties", {
                {"eventId", {{"type", "integer"}, {"description", "Event ID of the draw call"}}}
            }},
            {"required", json::array({"eventId"})}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            uint32_t eventId = args["eventId"].get<uint32_t>();
            const auto& actions = ctrl->GetRootActions();
            const SDFile& structuredFile = ctrl->GetStructuredFile();

            const ActionDescription* action = findActionByEventId(actions, eventId);
            if(!action)
                throw std::runtime_error("Event ID " + std::to_string(eventId) + " not found.");

            json result;
            result["eventId"] = action->eventId;
            result["name"] = std::string(action->GetName(structuredFile).c_str());
            result["flags"] = actionFlagsToString(action->flags);
            result["numIndices"] = action->numIndices;
            result["numInstances"] = action->numInstances;
            result["drawIndex"] = action->drawIndex;

            // Collect non-null output resource IDs
            json outputs = json::array();
            for(int i = 0; i < 8; i++)
            {
                if(action->outputs[i] != ResourceId::Null())
                    outputs.push_back(resourceIdToString(action->outputs[i]));
            }
            result["outputs"] = outputs;

            return result;
        }
    });
}
