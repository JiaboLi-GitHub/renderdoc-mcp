#pragma once
#include "core/types.h"
#include <optional>
#include <vector>

namespace renderdoc::core {

class Session;

std::vector<CounterInfo> listCounters(const Session& session);

CounterFetchResult fetchCounters(const Session& session,
                                  const std::vector<std::string>& counterNames = {},
                                  std::optional<uint32_t> eventId = std::nullopt);

} // namespace renderdoc::core
