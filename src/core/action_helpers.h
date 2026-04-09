#pragma once

#include "core/types.h"
#include <renderdoc_replay.h>

namespace renderdoc::core {

GraphicsApi toGraphicsApi(GraphicsAPI api);
uint32_t countAllEvents(const ActionDescription& action);
uint32_t countDrawCalls(const ActionDescription& action);

// Overloads for top-level action arrays
uint32_t countAllEvents(const rdcarray<ActionDescription>& actions);
uint32_t countDrawCalls(const rdcarray<ActionDescription>& actions);

} // namespace renderdoc::core
