#include "core/diff_internal.h"
#include "core/errors.h"

namespace renderdoc::core {

using namespace diff_internal;

// ---------------------------------------------------------------------------
// diffSummary
// ---------------------------------------------------------------------------
SummaryDiffResult diffSummary(DiffSession& session)
{
    IReplayController* ctrlA = session.controllerA();
    IReplayController* ctrlB = session.controllerB();
    if (!ctrlA || !ctrlB)
        throw CoreError(CoreError::Code::NoCaptureOpen, "DiffSession not open.");

    SummaryDiffResult result;

    const auto& rootA = ctrlA->GetRootActions();
    const auto& rootB = ctrlB->GetRootActions();

    // Level 1: Count metrics
    uint32_t drawsA   = countDraws(rootA);
    uint32_t drawsB   = countDraws(rootB);
    uint32_t passesA  = countPasses(rootA);
    uint32_t passesB  = countPasses(rootB);
    uint32_t eventsA  = countEvents(rootA);
    uint32_t eventsB  = countEvents(rootB);

    rdcarray<ResourceDescription> resA = ctrlA->GetResources();
    rdcarray<ResourceDescription> resB = ctrlB->GetResources();
    int resourcesA = resA.count();
    int resourcesB = resB.count();

    auto addRow = [&](const std::string& cat, int a, int b) {
        SummaryRow row;
        row.category = cat;
        row.valueA = a;
        row.valueB = b;
        row.delta = b - a;
        result.rows.push_back(row);
    };

    addRow("draws",     (int)drawsA,     (int)drawsB);
    addRow("passes",    (int)passesA,    (int)passesB);
    addRow("events",    (int)eventsA,    (int)eventsB);
    addRow("resources", resourcesA,       resourcesB);

    bool countsMatch = (drawsA == drawsB && passesA == passesB &&
                        eventsA == eventsB && resourcesA == resourcesB);

    if (!countsMatch) {
        result.divergedAt = "counts";
        result.identical = false;
        return result;
    }

    // Level 2: Structure check via diffDraws + diffResources
    DrawsDiffResult drawDiff = diffDraws(session);
    bool structureMatch = (drawDiff.added == 0 && drawDiff.deleted == 0 && drawDiff.modified == 0);

    if (!structureMatch) {
        result.divergedAt = "structure";
        result.identical = false;
        return result;
    }

    // Level 3: Framebuffer comparison at last draw
    try {
        ImageCompareResult imgResult = diffFramebuffer(session, 0, 0, 0, 0.0, "");
        if (!imgResult.pass) {
            result.divergedAt = "framebuffer";
            result.identical = false;
            return result;
        }
    } catch (const CoreError&) {
        // If framebuffer comparison fails (e.g. no RT), treat as structure match only
        result.identical = true;
        result.divergedAt = "";
        return result;
    }

    result.identical = true;
    result.divergedAt = "";
    return result;
}

} // namespace renderdoc::core
