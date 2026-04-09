#include "core/diff_internal.h"
#include "core/errors.h"

#include <algorithm>
#include <functional>
#include <unordered_map>

namespace renderdoc::core {

using namespace diff_internal;

// ---------------------------------------------------------------------------
// diffDraws
// ---------------------------------------------------------------------------
DrawsDiffResult diffDraws(DiffSession& session)
{
    IReplayController* ctrlA = session.controllerA();
    IReplayController* ctrlB = session.controllerB();
    if (!ctrlA || !ctrlB)
        throw CoreError(CoreError::Code::NoCaptureOpen, "DiffSession not open.");

    std::vector<DrawRecord> recsA = collectDrawRecords(ctrlA);
    std::vector<DrawRecord> recsB = collectDrawRecords(ctrlB);

    bool hasMarkersA = hasAnyMarker(recsA);
    bool hasMarkersB = hasAnyMarker(recsB);
    bool hasMarkers  = hasMarkersA || hasMarkersB;

    // Build match keys
    std::vector<std::string> keysA, keysB;
    keysA.reserve(recsA.size());
    keysB.reserve(recsB.size());
    for (const auto& r : recsA) keysA.push_back(makeDrawMatchKey(r, hasMarkers));
    for (const auto& r : recsB) keysB.push_back(makeDrawMatchKey(r, hasMarkers));

    // Count key occurrences to assess uniqueness
    std::unordered_map<std::string, int> keyCountA, keyCountB;
    for (const auto& k : keysA) keyCountA[k]++;
    for (const auto& k : keysB) keyCountB[k]++;

    // Run LCS alignment
    auto alignment = lcsAlign(keysA, keysB);

    DrawsDiffResult result;
    for (const auto& pair : alignment) {
        DrawDiffRow row;
        if (pair.first.has_value())  row.a = recsA[*pair.first];
        if (pair.second.has_value()) row.b = recsB[*pair.second];

        if (pair.first.has_value() && pair.second.has_value()) {
            // Matched -- check if truly equal or modified
            const std::string& keyA = keysA[*pair.first];
            bool unique = (keyCountA[keyA] == 1 && keyCountB[keyA] == 1);
            row.confidence = unique ? "high" : "low";

            // Equal if same key (keys include topology+shader or markerPath+drawType)
            row.status = DiffStatus::Equal;
            result.unchanged++;
        } else if (pair.first.has_value()) {
            row.status = DiffStatus::Deleted;
            row.confidence = "high";
            result.deleted++;
        } else {
            row.status = DiffStatus::Added;
            row.confidence = "high";
            result.added++;
        }

        result.rows.push_back(std::move(row));
    }

    return result;
}

// ---------------------------------------------------------------------------
// diffResources
// ---------------------------------------------------------------------------
ResourcesDiffResult diffResources(DiffSession& session)
{
    IReplayController* ctrlA = session.controllerA();
    IReplayController* ctrlB = session.controllerB();
    if (!ctrlA || !ctrlB)
        throw CoreError(CoreError::Code::NoCaptureOpen, "DiffSession not open.");

    rdcarray<ResourceDescription> resA = ctrlA->GetResources();
    rdcarray<ResourceDescription> resB = ctrlB->GetResources();

    // Build name->ResourceDescription map (first occurrence, named only)
    std::unordered_map<std::string, const ResourceDescription*> namedA, namedB;
    for (const auto& r : resA) {
        std::string name = toLower(std::string(r.name.c_str()));
        if (!name.empty() && namedA.find(name) == namedA.end())
            namedA[name] = &r;
    }
    for (const auto& r : resB) {
        std::string name = toLower(std::string(r.name.c_str()));
        if (!name.empty() && namedB.find(name) == namedB.end())
            namedB[name] = &r;
    }

    ResourcesDiffResult result;
    std::unordered_map<std::string, bool> matched;

    // Match named resources
    for (const auto& [name, rdA] : namedA) {
        ResourceDiffRow row;
        row.name = std::string(rdA->name.c_str());
        row.typeA = std::string(ToStr(rdA->type).c_str());
        row.confidence = "high";

        auto it = namedB.find(name);
        if (it != namedB.end()) {
            const ResourceDescription* rdB = it->second;
            row.typeB = std::string(ToStr(rdB->type).c_str());
            row.status = (row.typeA == row.typeB) ? DiffStatus::Equal : DiffStatus::Modified;
            if (row.status == DiffStatus::Equal) result.unchanged++;
            else result.modified++;
            matched[name] = true;
        } else {
            row.status = DiffStatus::Deleted;
            result.deleted++;
        }
        result.rows.push_back(std::move(row));
    }

    // Resources in B not in A (added named)
    for (const auto& [name, rdB] : namedB) {
        if (matched.find(name) == matched.end()) {
            ResourceDiffRow row;
            row.name = std::string(rdB->name.c_str());
            row.typeB = std::string(ToStr(rdB->type).c_str());
            row.status = DiffStatus::Added;
            row.confidence = "high";
            result.added++;
            result.rows.push_back(std::move(row));
        }
    }

    // Match unnamed resources positionally within type groups
    // Group unnamed resources by type
    std::unordered_map<std::string, std::vector<const ResourceDescription*>> unnamedByTypeA, unnamedByTypeB;
    for (const auto& r : resA) {
        std::string name = toLower(std::string(r.name.c_str()));
        if (name.empty()) {
            std::string type = std::string(ToStr(r.type).c_str());
            unnamedByTypeA[type].push_back(&r);
        }
    }
    for (const auto& r : resB) {
        std::string name = toLower(std::string(r.name.c_str()));
        if (name.empty()) {
            std::string type = std::string(ToStr(r.type).c_str());
            unnamedByTypeB[type].push_back(&r);
        }
    }

    // Collect all type keys
    std::unordered_map<std::string, bool> typesSeen;
    for (const auto& [t, _] : unnamedByTypeA) typesSeen[t] = true;
    for (const auto& [t, _] : unnamedByTypeB) typesSeen[t] = true;

    for (const auto& [type, _] : typesSeen) {
        auto& listA = unnamedByTypeA[type];
        auto& listB = unnamedByTypeB[type];
        size_t common = std::min(listA.size(), listB.size());

        for (size_t i = 0; i < common; ++i) {
            ResourceDiffRow row;
            row.name = "(unnamed " + type + " #" + std::to_string(i) + ")";
            row.typeA = type;
            row.typeB = type;
            row.status = DiffStatus::Equal;
            row.confidence = "low";
            result.unchanged++;
            result.rows.push_back(std::move(row));
        }
        for (size_t i = common; i < listA.size(); ++i) {
            ResourceDiffRow row;
            row.name = "(unnamed " + type + " #" + std::to_string(i) + ")";
            row.typeA = type;
            row.status = DiffStatus::Deleted;
            row.confidence = "low";
            result.deleted++;
            result.rows.push_back(std::move(row));
        }
        for (size_t i = common; i < listB.size(); ++i) {
            ResourceDiffRow row;
            row.name = "(unnamed " + type + " #" + std::to_string(i) + ")";
            row.typeB = type;
            row.status = DiffStatus::Added;
            row.confidence = "low";
            result.added++;
            result.rows.push_back(std::move(row));
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// diffStats
// ---------------------------------------------------------------------------
StatsDiffResult diffStats(DiffSession& session)
{
    IReplayController* ctrlA = session.controllerA();
    IReplayController* ctrlB = session.controllerB();
    if (!ctrlA || !ctrlB)
        throw CoreError(CoreError::Code::NoCaptureOpen, "DiffSession not open.");

    // Collect per-pass stats from a controller.
    // A "pass" is a top-level action with children.
    struct PassStats {
        std::string name;
        uint32_t draws = 0;
        uint32_t dispatches = 0;
        uint64_t triangles = 0;
    };

    auto collectPassStats = [&](IReplayController* ctrl) -> std::vector<PassStats> {
        std::vector<PassStats> passes;
        const auto& rootActions = ctrl->GetRootActions();

        std::function<void(const rdcarray<ActionDescription>&, PassStats&)> accum;
        accum = [&](const rdcarray<ActionDescription>& actions, PassStats& ps) {
            for (const auto& a : actions) {
                if (bool(a.flags & ActionFlags::Drawcall)) {
                    ps.draws++;
                    ps.triangles += computeTriangles(a.numIndices, a.numInstances);
                }
                if (bool(a.flags & ActionFlags::Dispatch)) {
                    ps.dispatches++;
                }
                if (!a.children.empty())
                    accum(a.children, ps);
            }
        };

        for (const auto& action : rootActions) {
            if (action.children.empty()) continue;
            PassStats ps;
            ps.name = std::string(action.customName.c_str());
            accum(action.children, ps);
            passes.push_back(std::move(ps));
        }
        // If no groups, create a single "default" pass
        if (passes.empty()) {
            PassStats ps;
            ps.name = "(default)";
            accum(rootActions, ps);
            passes.push_back(std::move(ps));
        }
        return passes;
    };

    auto passesA = collectPassStats(ctrlA);
    auto passesB = collectPassStats(ctrlB);

    // Build name map for B
    std::unordered_map<std::string, const PassStats*> mapB;
    for (const auto& p : passesB) {
        std::string key = toLower(p.name);
        if (mapB.find(key) == mapB.end())
            mapB[key] = &p;
    }

    StatsDiffResult result;
    std::unordered_map<std::string, bool> matched;

    // Match passes from A
    for (const auto& pa : passesA) {
        PassDiffRow row;
        row.name = pa.name;
        row.drawsA = pa.draws;
        row.trianglesA = pa.triangles;
        row.dispatchesA = pa.dispatches;

        std::string key = toLower(pa.name);
        auto it = mapB.find(key);
        if (it != mapB.end()) {
            const PassStats* pb = it->second;
            row.drawsB = pb->draws;
            row.trianglesB = pb->triangles;
            row.dispatchesB = pb->dispatches;

            bool equal = (pa.draws == pb->draws &&
                          pa.dispatches == pb->dispatches &&
                          pa.triangles == pb->triangles);
            row.status = equal ? DiffStatus::Equal : DiffStatus::Modified;
            if (!equal) result.passesChanged++;
            result.drawsDelta += (int64_t)pb->draws - (int64_t)pa.draws;
            result.trianglesDelta += (int64_t)pb->triangles - (int64_t)pa.triangles;
            result.dispatchesDelta += (int64_t)pb->dispatches - (int64_t)pa.dispatches;
            matched[key] = true;
        } else {
            row.status = DiffStatus::Deleted;
            result.passesDeleted++;
            result.drawsDelta -= (int64_t)pa.draws;
            result.trianglesDelta -= (int64_t)pa.triangles;
            result.dispatchesDelta -= (int64_t)pa.dispatches;
        }
        result.rows.push_back(std::move(row));
    }

    // Passes in B not in A
    for (const auto& pb : passesB) {
        std::string key = toLower(pb.name);
        if (matched.find(key) == matched.end()) {
            PassDiffRow row;
            row.name = pb.name;
            row.drawsB = pb.draws;
            row.trianglesB = pb.triangles;
            row.dispatchesB = pb.dispatches;
            row.status = DiffStatus::Added;
            result.passesAdded++;
            result.drawsDelta += (int64_t)pb.draws;
            result.trianglesDelta += (int64_t)pb.triangles;
            result.dispatchesDelta += (int64_t)pb.dispatches;
            result.rows.push_back(std::move(row));
        }
    }

    return result;
}

} // namespace renderdoc::core
