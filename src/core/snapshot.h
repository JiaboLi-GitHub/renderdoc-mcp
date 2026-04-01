#pragma once

#include "core/types.h"
#include <functional>
#include <string>

namespace renderdoc::core {

class Session;

SnapshotResult exportSnapshot(Session& session, uint32_t eventId,
                              const std::string& outputDir,
                              std::function<std::string(const PipelineState&)> pipelineSerializer);

} // namespace renderdoc::core
