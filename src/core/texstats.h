#pragma once

#include "core/types.h"
#include "core/session.h"
#include <optional>

namespace renderdoc::core {

TextureStats getTextureStats(
    const Session& session,
    ResourceId resourceId,
    uint32_t mip = 0,
    uint32_t slice = 0,
    bool histogram = false,
    std::optional<uint32_t> eventId = std::nullopt);

} // namespace renderdoc::core
