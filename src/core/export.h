#pragma once
#include "core/types.h"
#include <string>

namespace renderdoc::core {

class Session;

// Export the render target at the given index for the current event.
// rtIndex must be in [0, 7]. Output is written as PNG to outputDir.
ExportResult exportRenderTarget(const Session& session,
                                int rtIndex,
                                const std::string& outputDir);

// Export a texture resource as PNG. mip and layer default to 0.
ExportResult exportTexture(const Session& session,
                           ResourceId id,
                           const std::string& outputDir,
                           uint32_t mip = 0,
                           uint32_t layer = 0);

// Export buffer data to a binary file. size == 0 means read all bytes.
ExportResult exportBuffer(const Session& session,
                          ResourceId id,
                          const std::string& outputDir,
                          uint64_t offset = 0,
                          uint64_t size = 0);

} // namespace renderdoc::core
