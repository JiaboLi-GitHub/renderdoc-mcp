#pragma once

#include "core/types.h"
#include <optional>
#include <string>
#include <vector>

namespace renderdoc::core {

class Session;

std::vector<EventInfo> listEvents(const Session& session,
                                   const std::string& filter = "");

std::vector<EventInfo> listDraws(const Session& session,
                                  const std::string& filter = "",
                                  uint32_t limit = 1000);

EventInfo getDrawInfo(const Session& session, uint32_t eventId);

EventInfo gotoEvent(Session& session, uint32_t eventId);

} // namespace renderdoc::core
