#pragma once
#include "core/types.h"
#include <optional>
#include <vector>

namespace renderdoc::core {

class Session;

std::vector<CBufferInfo> listCBuffers(const Session& session,
                                       ShaderStage stage,
                                       std::optional<uint32_t> eventId = std::nullopt);

CBufferContents getCBufferContents(const Session& session,
                                    ShaderStage stage,
                                    uint32_t cbufferIndex,
                                    std::optional<uint32_t> eventId = std::nullopt);

} // namespace renderdoc::core
