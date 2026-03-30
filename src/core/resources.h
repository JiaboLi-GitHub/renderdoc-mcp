#pragma once

#include "core/types.h"
#include <optional>
#include <string>
#include <vector>

namespace renderdoc::core {

class Session;

std::vector<ResourceInfo> listResources(const Session& session,
                                         const std::string& typeFilter = "",
                                         const std::string& nameFilter = "");

ResourceInfo getResourceDetails(const Session& session, ResourceId id);

std::vector<PassInfo> listPasses(const Session& session);

PassInfo getPassInfo(const Session& session, uint32_t eventId);

} // namespace renderdoc::core
