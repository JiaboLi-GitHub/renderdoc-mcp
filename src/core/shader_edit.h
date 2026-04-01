#pragma once

#include "core/types.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace renderdoc::core {

class Session;

struct ShaderEditState {
    std::map<uint64_t, uint64_t> builtShaders;
    std::map<uint64_t, uint64_t> shaderReplacements;
};

std::vector<std::string> getShaderEncodings(const Session& session);

ShaderBuildResult buildShader(Session& session,
                              const std::string& source,
                              ShaderStage stage,
                              const std::string& entry,
                              const std::string& encoding);

uint64_t replaceShader(Session& session,
                       uint32_t eventId,
                       ShaderStage stage,
                       uint64_t shaderId);

void restoreShader(Session& session,
                   uint32_t eventId,
                   ShaderStage stage);

std::pair<int, int> restoreAllShaders(Session& session);

void cleanupShaderEdits(Session& session);

} // namespace renderdoc::core
