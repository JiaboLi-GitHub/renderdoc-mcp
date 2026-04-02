#pragma once

#include "core/types.h"

namespace renderdoc::core {

class Session;

/// Enumerate passes using hybrid resolution: marker-based passes plus
/// synthetic gap-fill for uncovered draw/dispatch events.
/// Each PassRange includes firstDrawEventId for safe pipeline state sampling.
std::vector<PassRange> enumeratePassRanges(const Session& session);

/// Query color and depth attachments for a pass identified by eventId.
/// eventId can be any event within a pass range.
PassAttachments getPassAttachments(const Session& session, uint32_t eventId);

/// Return per-pass aggregated statistics for the entire frame.
std::vector<PassStatistics> getPassStatistics(const Session& session);

/// Build inter-pass resource dependency DAG.
PassDependencyGraph getPassDependencies(const Session& session);

/// Detect render targets written but never consumed by visible output.
UnusedTargetResult findUnusedTargets(const Session& session);

} // namespace renderdoc::core
