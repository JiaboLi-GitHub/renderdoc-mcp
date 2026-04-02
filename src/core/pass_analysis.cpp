#include "core/pass_analysis.h"
#include "core/errors.h"
#include "core/pipeline.h"
#include "core/resources.h"
#include "core/session.h"
#include "core/usage.h"

#include <renderdoc_replay.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace renderdoc::core {

namespace {

ResourceId toResourceId(::ResourceId id) {
    static_assert(sizeof(::ResourceId) == sizeof(uint64_t), "ResourceId size mismatch");
    uint64_t raw = 0;
    std::memcpy(&raw, &id, sizeof(raw));
    return raw;
}

::ResourceId fromResourceId(uint64_t raw) {
    static_assert(sizeof(::ResourceId) == sizeof(uint64_t), "ResourceId size mismatch");
    ::ResourceId id;
    std::memcpy(&id, &raw, sizeof(id));
    return id;
}

uint32_t lastEventId(const ActionDescription& action) {
    if (!action.children.empty())
        return lastEventId(action.children.back());
    return action.eventId;
}

bool hasDrawsOrDispatches(const rdcarray<ActionDescription>& actions) {
    for (const auto& a : actions) {
        if (bool(a.flags & ActionFlags::Drawcall) || bool(a.flags & ActionFlags::Dispatch))
            return true;
        if (!a.children.empty() && hasDrawsOrDispatches(a.children))
            return true;
    }
    return false;
}

uint64_t countTriangles(const rdcarray<ActionDescription>& actions) {
    uint64_t total = 0;
    for (const auto& a : actions) {
        if (bool(a.flags & ActionFlags::Drawcall))
            total += static_cast<uint64_t>(a.numIndices) * std::max(a.numInstances, 1u) / 3;
        if (!a.children.empty())
            total += countTriangles(a.children);
    }
    return total;
}

void countActions(const rdcarray<ActionDescription>& actions,
                  uint32_t& draws, uint32_t& dispatches) {
    for (const auto& a : actions) {
        if (bool(a.flags & ActionFlags::Drawcall)) draws++;
        if (bool(a.flags & ActionFlags::Dispatch)) dispatches++;
        if (!a.children.empty())
            countActions(a.children, draws, dispatches);
    }
}

void collectActionsInRange(const rdcarray<ActionDescription>& actions,
                           uint32_t beginEid, uint32_t endEid,
                           uint32_t& draws, uint32_t& dispatches, uint64_t& triangles) {
    for (const auto& a : actions) {
        if (a.eventId >= beginEid && a.eventId <= endEid) {
            if (bool(a.flags & ActionFlags::Drawcall)) {
                draws++;
                triangles += static_cast<uint64_t>(a.numIndices) * std::max(a.numInstances, 1u) / 3;
            }
            if (bool(a.flags & ActionFlags::Dispatch))
                dispatches++;
        }
        if (!a.children.empty())
            collectActionsInRange(a.children, beginEid, endEid, draws, dispatches, triangles);
    }
}

struct RTKey {
    std::vector<ResourceId> colorIds;
    ResourceId depthId = 0;
    bool operator==(const RTKey& o) const { return colorIds == o.colorIds && depthId == o.depthId; }
    bool operator!=(const RTKey& o) const { return !(*this == o); }
    bool empty() const { return colorIds.empty() && depthId == 0; }
};

// Read RT key from ActionDescription::outputs / depthOut (no PipeState virtual call).
// This avoids GetPipelineState() which is not exported from renderdoc.dll.
RTKey getRTKey(const ActionDescription& action) {
    RTKey key;
    for (int i = 0; i < static_cast<int>(action.outputs.size()); i++) {
        if (action.outputs[i] != ::ResourceId::Null())
            key.colorIds.push_back(toResourceId(action.outputs[i]));
    }
    if (action.depthOut != ::ResourceId::Null())
        key.depthId = toResourceId(action.depthOut);
    return key;
}

std::string syntheticPassName(const RTKey& key) {
    if (key.empty()) return "No-RT";
    std::string name;
    for (size_t i = 0; i < key.colorIds.size(); i++) {
        if (i > 0) name += "+";
        name += "RT" + std::to_string(i);
    }
    if (key.depthId != 0) {
        if (!name.empty()) name += "+";
        name += "Depth";
    }
    return name;
}

// FlatEvent stores ActionFlags (not uint32_t) so bitwise & with ActionFlags works directly.
struct FlatEvent {
    uint32_t eventId;
    ActionFlags flags;
    RTKey rtKey;
};

void flattenActions(const rdcarray<ActionDescription>& actions,
                    std::vector<FlatEvent>& out) {
    for (const auto& a : actions) {
        if (bool(a.flags & ActionFlags::Drawcall) ||
            bool(a.flags & ActionFlags::Dispatch) ||
            bool(a.flags & ActionFlags::Clear) ||
            bool(a.flags & ActionFlags::Copy))
            out.push_back({a.eventId, a.flags, getRTKey(a)});
        if (!a.children.empty())
            flattenActions(a.children, out);
    }
}

std::vector<PassRange> buildSyntheticRanges(
    const std::vector<FlatEvent>& events)
{
    std::vector<PassRange> result;
    if (events.empty()) return result;

    RTKey currentKey = events[0].rtKey;
    uint32_t groupBegin = events[0].eventId;
    uint32_t groupEnd = events[0].eventId;
    bool groupHasDrawOrDispatch = bool(events[0].flags & ActionFlags::Drawcall) ||
                                   bool(events[0].flags & ActionFlags::Dispatch);
    uint32_t firstDraw = groupHasDrawOrDispatch ? events[0].eventId : 0;

    auto emitGroup = [&]() {
        if (!groupHasDrawOrDispatch) return;
        PassRange pr;
        pr.name = syntheticPassName(currentKey);
        pr.beginEventId = groupBegin;
        pr.endEventId = groupEnd;
        pr.firstDrawEventId = firstDraw;
        pr.synthetic = true;
        result.push_back(std::move(pr));
    };

    for (size_t i = 1; i < events.size(); i++) {
        bool isBoundary = bool(events[i].flags & ActionFlags::Clear) ||
                          bool(events[i].flags & ActionFlags::Copy);
        const RTKey& key = events[i].rtKey;

        if (key != currentKey || isBoundary) {
            emitGroup();
            currentKey = key;
            groupBegin = events[i].eventId;
            groupHasDrawOrDispatch = false;
            firstDraw = 0;
        }

        bool isDrawOrDispatch = bool(events[i].flags & ActionFlags::Drawcall) ||
                                bool(events[i].flags & ActionFlags::Dispatch);
        if (isDrawOrDispatch) {
            groupHasDrawOrDispatch = true;
            if (firstDraw == 0) firstDraw = events[i].eventId;
        }

        groupEnd = events[i].eventId;
    }
    emitGroup();

    return result;
}

bool isWriteUsage(::ResourceUsage usage) {
    switch (usage) {
        case ::ResourceUsage::ColorTarget:
        case ::ResourceUsage::DepthStencilTarget:
        case ::ResourceUsage::CopyDst:
        case ::ResourceUsage::Clear:
        case ::ResourceUsage::GenMips:
        case ::ResourceUsage::ResolveDst:
            return true;
        default:
            return false;
    }
}

bool isReadUsage(::ResourceUsage usage) {
    switch (usage) {
        case ::ResourceUsage::VertexBuffer:
        case ::ResourceUsage::IndexBuffer:
        case ::ResourceUsage::VS_Constants: case ::ResourceUsage::HS_Constants:
        case ::ResourceUsage::DS_Constants: case ::ResourceUsage::GS_Constants:
        case ::ResourceUsage::PS_Constants: case ::ResourceUsage::CS_Constants:
        case ::ResourceUsage::VS_Resource: case ::ResourceUsage::HS_Resource:
        case ::ResourceUsage::DS_Resource: case ::ResourceUsage::GS_Resource:
        case ::ResourceUsage::PS_Resource: case ::ResourceUsage::CS_Resource:
        case ::ResourceUsage::Indirect:
        case ::ResourceUsage::InputTarget:
        case ::ResourceUsage::CopySrc:
        case ::ResourceUsage::ResolveSrc:
            return true;
        default:
            return false;
    }
}

int findPassIndex(const std::vector<PassRange>& passes, uint32_t eventId) {
    for (size_t i = 0; i < passes.size(); i++) {
        if (eventId >= passes[i].beginEventId && eventId <= passes[i].endEventId)
            return static_cast<int>(i);
    }
    return -1;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// enumeratePassRanges
// ---------------------------------------------------------------------------

std::vector<PassRange> enumeratePassRanges(const Session& session) {
    auto* ctrl = session.controller();
    const auto& rootActions = ctrl->GetRootActions();
    const SDFile& sf = ctrl->GetStructuredFile();

    // Step 1: Collect marker-based passes, resolving firstDrawEventId.
    std::vector<PassRange> markerPasses;
    for (const auto& action : rootActions) {
        if (action.children.empty()) continue;
        if (!hasDrawsOrDispatches(action.children)) continue;

        PassRange pr;
        pr.name = std::string(action.GetName(sf).c_str());
        pr.beginEventId = action.eventId;
        pr.endEventId = lastEventId(action);
        pr.synthetic = false;

        std::vector<FlatEvent> childEvents;
        flattenActions(action.children, childEvents);
        for (const auto& ce : childEvents) {
            if (bool(ce.flags & ActionFlags::Drawcall) ||
                bool(ce.flags & ActionFlags::Dispatch)) {
                pr.firstDrawEventId = ce.eventId;
                break;
            }
        }

        markerPasses.push_back(std::move(pr));
    }

    // Step 2: Collect ALL flat events for gap detection.
    std::vector<FlatEvent> allEvents;
    flattenActions(rootActions, allEvents);
    std::sort(allEvents.begin(), allEvents.end(),
              [](const FlatEvent& a, const FlatEvent& b) { return a.eventId < b.eventId; });

    if (markerPasses.empty()) {
        return buildSyntheticRanges(allEvents);
    }

    // Step 3: Hybrid merge — marker passes + synthetic ranges for gaps.
    std::vector<FlatEvent> uncoveredEvents;
    for (const auto& ev : allEvents) {
        bool covered = false;
        for (const auto& mp : markerPasses) {
            if (ev.eventId >= mp.beginEventId && ev.eventId <= mp.endEventId) {
                covered = true;
                break;
            }
        }
        if (!covered)
            uncoveredEvents.push_back(ev);
    }

    auto syntheticGaps = buildSyntheticRanges(uncoveredEvents);

    std::vector<PassRange> result;
    result.reserve(markerPasses.size() + syntheticGaps.size());
    result.insert(result.end(), markerPasses.begin(), markerPasses.end());
    result.insert(result.end(), syntheticGaps.begin(), syntheticGaps.end());
    std::sort(result.begin(), result.end(),
              [](const PassRange& a, const PassRange& b) {
                  return a.beginEventId < b.beginEventId;
              });

    return result;
}

} // namespace renderdoc::core
