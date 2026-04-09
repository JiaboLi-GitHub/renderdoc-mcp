#include "core/events.h"
#include "core/constants.h"
#include "core/errors.h"
#include "core/resource_id.h"
#include "core/session.h"

#include <renderdoc_replay.h>

#include <algorithm>
#include <cctype>

namespace renderdoc::core {

namespace {

// Build an EventInfo from an ActionDescription.
EventInfo makeEventInfo(const ActionDescription& action) {
    EventInfo info;
    info.eventId = action.eventId;
    info.name = action.customName.c_str();
    info.flags = static_cast<ActionFlagBits>(action.flags);
    info.numIndices = action.numIndices;
    info.numInstances = action.numInstances;
    info.drawIndex = action.drawIndex;
    return info;
}

// Recursive helper: collect all events (optionally filtered by name).
void collectEvents(const rdcarray<ActionDescription>& actions,
                   const std::string& filter,
                   std::vector<EventInfo>& out) {
    for (const auto& action : actions) {
        EventInfo info = makeEventInfo(action);

        if (filter.empty()) {
            out.push_back(std::move(info));
        } else {
            std::string lowerName = info.name;
            std::string lowerFilter = filter;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
            if (lowerName.find(lowerFilter) != std::string::npos)
                out.push_back(std::move(info));
        }

        if (!action.children.empty())
            collectEvents(action.children, filter, out);
    }
}

// Recursive helper: collect only Drawcall-flagged actions, with filter and limit.
void collectDrawCalls(const rdcarray<ActionDescription>& actions,
                      const std::string& filter,
                      uint32_t limit,
                      std::vector<EventInfo>& out) {
    for (const auto& action : actions) {
        if (out.size() >= limit)
            return;

        if (action.flags & ActionFlags::Drawcall) {
            EventInfo info = makeEventInfo(action);

            bool matches = true;
            if (!filter.empty()) {
                std::string lowerName = info.name;
                std::string lowerFilter = filter;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
                matches = (lowerName.find(lowerFilter) != std::string::npos);
            }

            if (matches)
                out.push_back(std::move(info));
        }

        if (!action.children.empty())
            collectDrawCalls(action.children, filter, limit, out);
    }
}

// Recursive helper: find an action by event ID.
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

std::vector<EventInfo> listEvents(const Session& session, const std::string& filter) {
    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open
    const auto& actions = ctrl->GetRootActions();

    std::vector<EventInfo> result;
    collectEvents(actions, filter, result);
    return result;
}

std::vector<EventInfo> listDraws(const Session& session,
                                  const std::string& filter,
                                  uint32_t limit) {
    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open
    const auto& actions = ctrl->GetRootActions();

    std::vector<EventInfo> result;
    collectDrawCalls(actions, filter, limit, result);
    return result;
}

EventInfo getDrawInfo(const Session& session, uint32_t eventId) {
    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open
    const auto& actions = ctrl->GetRootActions();

    const ActionDescription* action = findActionByEventId(actions, eventId);
    if (!action)
        throw CoreError(CoreError::Code::InvalidEventId,
                        "Event ID " + std::to_string(eventId) + " not found.");

    EventInfo info = makeEventInfo(*action);

    // Collect non-null output resource IDs
    for (int i = 0; i < kMaxRenderTargets; i++) {
        if (action->outputs[i] != ::ResourceId::Null())
            info.outputs.push_back(toResourceId(action->outputs[i]));
    }

    return info;
}

EventInfo gotoEvent(Session& session, uint32_t eventId) {
    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open
    const auto& actions = ctrl->GetRootActions();

    const ActionDescription* action = findActionByEventId(actions, eventId);
    if (!action)
        throw CoreError(CoreError::Code::InvalidEventId,
                        "Event ID " + std::to_string(eventId) + " not found.");

    ctrl->SetFrameEvent(eventId, true);
    session.setCurrentEventId(eventId);

    return makeEventInfo(*action);
}

} // namespace renderdoc::core
