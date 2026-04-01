#pragma once

#include "core/types.h"
#include <optional>
#include <string>

namespace renderdoc::core {

class Session;

MeshData exportMesh(const Session& session, uint32_t eventId,
                    MeshStage stage = MeshStage::VSOut);
std::string meshToObj(const MeshData& data);

} // namespace renderdoc::core
