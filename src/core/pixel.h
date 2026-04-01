#pragma once

#include "core/types.h"
#include "core/session.h"
#include <optional>

namespace renderdoc::core {

PixelHistoryResult pixelHistory(
    const Session& session,
    uint32_t x, uint32_t y,
    uint32_t targetIndex = 0,
    std::optional<uint32_t> eventId = std::nullopt);

PickPixelResult pickPixel(
    const Session& session,
    uint32_t x, uint32_t y,
    uint32_t targetIndex = 0,
    std::optional<uint32_t> eventId = std::nullopt);

} // namespace renderdoc::core
