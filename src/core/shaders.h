#pragma once

#include "core/constants.h"
#include "core/types.h"
#include <optional>
#include <string>
#include <vector>

namespace renderdoc::core {

class Session;

ShaderReflection getShaderReflection(const Session& session,
                                      ShaderStage stage,
                                      std::optional<uint32_t> eventId = std::nullopt);

ShaderDisassembly getShaderDisassembly(const Session& session,
                                        ShaderStage stage,
                                        std::optional<uint32_t> eventId = std::nullopt);

std::vector<ShaderUsageInfo> listShaders(const Session& session);

std::vector<ShaderSearchMatch> searchShaders(const Session& session,
                                              const std::string& pattern,
                                              std::optional<ShaderStage> stage = std::nullopt,
                                              uint32_t limit = kDefaultShaderSearchLimit);

} // namespace renderdoc::core
