#pragma once

#include "core/types.h"
#include "core/session.h"
#include <optional>

namespace renderdoc::core {

ShaderDebugResult debugPixel(
    const Session& session,
    uint32_t eventId,
    uint32_t x, uint32_t y,
    bool fullTrace = false,
    uint32_t primitive = 0xFFFFFFFF);

ShaderDebugResult debugVertex(
    const Session& session,
    uint32_t eventId,
    uint32_t vertexId,
    bool fullTrace = false,
    uint32_t instance = 0,
    uint32_t index = 0xFFFFFFFF,
    uint32_t view = 0);

ShaderDebugResult debugThread(
    const Session& session,
    uint32_t eventId,
    uint32_t groupX, uint32_t groupY, uint32_t groupZ,
    uint32_t threadX, uint32_t threadY, uint32_t threadZ,
    bool fullTrace = false);

} // namespace renderdoc::core
