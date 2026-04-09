#include "core/pass_analysis.h"
#include "core/errors.h"
#include "core/pipeline.h"
#include "core/resource_id.h"
#include "core/resources.h"
#include "core/session.h"
#include "core/usage.h"

#include <renderdoc_replay.h>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace renderdoc::core {

namespace {

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

// ---------------------------------------------------------------------------
// getPassAttachments
// ---------------------------------------------------------------------------

PassAttachments getPassAttachments(const Session& session, uint32_t eventId) {
    auto* ctrl = session.controller();

    auto ranges = enumeratePassRanges(session);

    // Find the pass that contains eventId
    const PassRange* found = nullptr;
    for (const auto& pr : ranges) {
        if (pr.beginEventId == eventId || pr.endEventId == eventId ||
            (eventId >= pr.beginEventId && eventId <= pr.endEventId)) {
            found = &pr;
            break;
        }
    }

    if (!found)
        throw CoreError(CoreError::Code::InvalidEventId,
                        "Event ID " + std::to_string(eventId) + " does not belong to any pass.");

    if (found->firstDrawEventId == 0)
        throw CoreError(CoreError::Code::InternalError,
                        "Pass '" + found->name + "' has no draw/dispatch events.");

    // Navigate to the first actual draw (not the marker) to read pipeline state.
    ctrl->SetFrameEvent(found->firstDrawEventId, true);
    auto pipeState = getPipelineState(session);

    PassAttachments pa;
    pa.passName = found->name;
    pa.eventId = found->beginEventId;
    pa.synthetic = found->synthetic;

    for (const auto& rt : pipeState.renderTargets) {
        AttachmentInfo ai;
        ai.resourceId = rt.id;
        ai.name = rt.name;
        ai.format = rt.format;
        ai.width = rt.width;
        ai.height = rt.height;
        pa.colorTargets.push_back(std::move(ai));
    }

    if (pipeState.depthTarget) {
        pa.hasDepth = true;
        pa.depthTarget.resourceId = pipeState.depthTarget->id;
        pa.depthTarget.name = pipeState.depthTarget->name;
        pa.depthTarget.format = pipeState.depthTarget->format;
        pa.depthTarget.width = pipeState.depthTarget->width;
        pa.depthTarget.height = pipeState.depthTarget->height;
    }

    return pa;
}

// ---------------------------------------------------------------------------
// getPassStatistics
// ---------------------------------------------------------------------------

std::vector<PassStatistics> getPassStatistics(const Session& session) {
    auto* ctrl = session.controller();
    const auto& rootActions = ctrl->GetRootActions();

    auto ranges = enumeratePassRanges(session);
    std::vector<PassStatistics> result;

    for (const auto& pr : ranges) {
        PassStatistics ps;
        ps.name = pr.name;
        ps.eventId = pr.beginEventId;
        ps.synthetic = pr.synthetic;
        ps.drawCount = 0;
        ps.dispatchCount = 0;
        ps.totalTriangles = 0;

        collectActionsInRange(rootActions, pr.beginEventId, pr.endEventId,
                              ps.drawCount, ps.dispatchCount, ps.totalTriangles);

        // Get RT dimensions from pipeline state at the first actual draw.
        if (pr.firstDrawEventId == 0) {
            result.push_back(std::move(ps));
            continue;
        }
        ctrl->SetFrameEvent(pr.firstDrawEventId, true);
        auto pipeState = getPipelineState(session);

        if (!pipeState.renderTargets.empty()) {
            ps.rtWidth = pipeState.renderTargets[0].width;
            ps.rtHeight = pipeState.renderTargets[0].height;
        }
        ps.attachmentCount = static_cast<uint32_t>(pipeState.renderTargets.size());
        if (pipeState.depthTarget) ps.attachmentCount++;

        result.push_back(std::move(ps));
    }

    return result;
}

// ---------------------------------------------------------------------------
// getPassDependencies
// ---------------------------------------------------------------------------

PassDependencyGraph getPassDependencies(const Session& session) {
    auto* ctrl = session.controller();
    auto passes = enumeratePassRanges(session);

    const auto& textures = ctrl->GetTextures();
    const auto& buffers = ctrl->GetBuffers();

    std::vector<ResourceId> resourceIds;
    for (int i = 0; i < textures.count(); i++)
        resourceIds.push_back(toResourceId(textures[i].resourceId));
    for (int i = 0; i < buffers.count(); i++)
        resourceIds.push_back(toResourceId(buffers[i].resourceId));

    std::map<ResourceId, std::set<int>> writers, readers;

    for (auto rid : resourceIds) {
        ::ResourceId rdcId = fromResourceId(rid);
        rdcarray<EventUsage> usages = ctrl->GetUsage(rdcId);

        for (int j = 0; j < usages.count(); j++) {
            int pi = findPassIndex(passes, usages[j].eventId);
            if (pi < 0) continue;
            if (isWriteUsage(usages[j].usage)) writers[rid].insert(pi);
            if (isReadUsage(usages[j].usage))  readers[rid].insert(pi);
        }
    }

    std::map<std::pair<int,int>, std::vector<ResourceId>> edgeMap;
    for (const auto& [rid, ws] : writers) {
        auto it = readers.find(rid);
        if (it == readers.end()) continue;
        for (int w : ws) {
            for (int r : it->second) {
                if (w < r)
                    edgeMap[{w, r}].push_back(rid);
            }
        }
    }

    PassDependencyGraph graph;
    graph.passCount = static_cast<uint32_t>(passes.size());

    for (const auto& [key, rids] : edgeMap) {
        PassEdge edge;
        edge.srcPass = passes[key.first].name;
        edge.dstPass = passes[key.second].name;
        edge.sharedResources = rids;
        graph.edges.push_back(std::move(edge));
    }
    graph.edgeCount = static_cast<uint32_t>(graph.edges.size());

    return graph;
}

// ---------------------------------------------------------------------------
// findUnusedTargets
// ---------------------------------------------------------------------------

UnusedTargetResult findUnusedTargets(const Session& session) {
    auto* ctrl = session.controller();
    auto passes = enumeratePassRanges(session);

    if (passes.empty())
        return {};

    const auto& textures = ctrl->GetTextures();
    const auto& buffers = ctrl->GetBuffers();

    std::vector<ResourceId> allResourceIds;
    for (int i = 0; i < textures.count(); i++)
        allResourceIds.push_back(toResourceId(textures[i].resourceId));
    for (int i = 0; i < buffers.count(); i++)
        allResourceIds.push_back(toResourceId(buffers[i].resourceId));

    struct TargetData {
        std::string name;
        std::set<int> writtenByPasses;
    };
    std::map<ResourceId, TargetData> writeTargets;
    std::map<ResourceId, std::set<int>> readers;

    std::set<ResourceId> swapchainIds;
    {
        const auto& resDescs = ctrl->GetResources();
        for (int i = 0; i < resDescs.count(); i++) {
            if (resDescs[i].type == ResourceType::SwapchainImage)
                swapchainIds.insert(toResourceId(resDescs[i].resourceId));
        }
    }

    std::map<ResourceId, std::string> nameMap;
    {
        const auto& resDescs = ctrl->GetResources();
        for (int i = 0; i < resDescs.count(); i++)
            nameMap[toResourceId(resDescs[i].resourceId)] = std::string(resDescs[i].name.c_str());
    }

    for (auto rid : allResourceIds) {
        ::ResourceId rdcId = fromResourceId(rid);
        rdcarray<EventUsage> usages = ctrl->GetUsage(rdcId);

        for (int j = 0; j < usages.count(); j++) {
            int pi = findPassIndex(passes, usages[j].eventId);
            if (pi < 0) continue;

            if (isWriteUsage(usages[j].usage)) {
                auto& td = writeTargets[rid];
                td.name = nameMap.count(rid) ? nameMap[rid] : "";
                td.writtenByPasses.insert(pi);
            }
            if (isReadUsage(usages[j].usage)) {
                readers[rid].insert(pi);
            }
        }
    }

    // Mark always-live resources.
    std::set<ResourceId> live(swapchainIds.begin(), swapchainIds.end());

    // Reverse reachability — iterate until convergence.
    bool changed = true;
    while (changed) {
        changed = false;
        for (int pi = static_cast<int>(passes.size()) - 1; pi >= 0; pi--) {
            bool writesLive = false;
            for (const auto& [rid, td] : writeTargets) {
                if (td.writtenByPasses.count(pi) && live.count(rid)) {
                    writesLive = true;
                    break;
                }
            }
            if (!writesLive) continue;

            for (const auto& [rid, passSet] : readers) {
                if (passSet.count(pi) && live.count(rid) == 0) {
                    live.insert(rid);
                    changed = true;
                }
            }
        }
    }

    // Collect unused resources.
    std::set<ResourceId> unusedSet;
    for (const auto& [rid, td] : writeTargets) {
        if (!live.count(rid))
            unusedSet.insert(rid);
    }

    // Wave assignment via iterative leaf pruning.
    std::map<ResourceId, uint32_t> waveMap;
    std::set<ResourceId> remaining = unusedSet;
    uint32_t currentWave = 1;

    while (!remaining.empty()) {
        std::set<ResourceId> thisWave;
        for (auto rid : remaining) {
            bool hasRemainingConsumer = false;
            auto readIt = readers.find(rid);
            if (readIt != readers.end()) {
                for (int pi : readIt->second) {
                    bool readerOutputsResolved = true;
                    for (const auto& [wrid, wtd] : writeTargets) {
                        if (wtd.writtenByPasses.count(pi) && remaining.count(wrid) &&
                            wrid != rid) {
                            readerOutputsResolved = false;
                            break;
                        }
                    }
                    if (!readerOutputsResolved) {
                        hasRemainingConsumer = true;
                        break;
                    }
                }
            }
            if (!hasRemainingConsumer)
                thisWave.insert(rid);
        }

        if (thisWave.empty()) {
            for (auto rid : remaining)
                waveMap[rid] = currentWave;
            remaining.clear();
        } else {
            for (auto rid : thisWave) {
                waveMap[rid] = currentWave;
                remaining.erase(rid);
            }
            currentWave++;
        }
    }

    // Build result.
    UnusedTargetResult result;
    result.totalTargets = static_cast<uint32_t>(writeTargets.size());

    for (const auto& [rid, td] : writeTargets) {
        if (live.count(rid)) continue;

        UnusedTarget ut;
        ut.resourceId = rid;
        ut.name = td.name;
        for (int pi : td.writtenByPasses)
            ut.writtenBy.push_back(passes[pi].name);
        ut.wave = waveMap.count(rid) ? waveMap[rid] : 1;
        result.unused.push_back(std::move(ut));
    }

    result.unusedCount = static_cast<uint32_t>(result.unused.size());
    return result;
}

} // namespace renderdoc::core
