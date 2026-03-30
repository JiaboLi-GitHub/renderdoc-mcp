#pragma once

#include "core/types.h"
#include <optional>
#include <string>
#include <vector>

namespace renderdoc::core {

class Session;

CaptureInfo getCaptureInfo(const Session& session);
CaptureStats getStats(const Session& session);
std::vector<DebugMessage> getLog(const Session& session,
                                  const std::string& minSeverity = "",
                                  std::optional<uint32_t> eventId = std::nullopt);

} // namespace renderdoc::core
