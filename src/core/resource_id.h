#pragma once

#include "core/types.h"
#include <renderdoc_replay.h>
#include <cstring>

namespace renderdoc::core {

inline ResourceId toResourceId(::ResourceId id) {
    static_assert(sizeof(::ResourceId) == sizeof(uint64_t), "ResourceId size mismatch");
    uint64_t raw = 0;
    std::memcpy(&raw, &id, sizeof(raw));
    return raw;
}

inline ::ResourceId fromResourceId(ResourceId id) {
    static_assert(sizeof(::ResourceId) == sizeof(uint64_t), "ResourceId size mismatch");
    ::ResourceId rid;
    std::memcpy(&rid, &id, sizeof(rid));
    return rid;
}

} // namespace renderdoc::core
