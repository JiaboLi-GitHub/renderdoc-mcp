#pragma once
#include "core/types.h"
#include <map>
#include <optional>

namespace renderdoc::core {

class Session;

PipelineState getPipelineState(const Session& session,
                                std::optional<uint32_t> eventId = std::nullopt);

std::map<ShaderStage, StageBindings> getBindings(
    const Session& session,
    std::optional<uint32_t> eventId = std::nullopt);

} // namespace renderdoc::core
