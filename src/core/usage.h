#pragma once

#include "core/types.h"

namespace renderdoc::core {

class Session;

ResourceUsageResult getResourceUsage(const Session& session, ResourceId resourceId);

} // namespace renderdoc::core
