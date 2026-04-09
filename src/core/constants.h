#pragma once
#include <cstdint>

namespace renderdoc::core {

constexpr int kMaxRenderTargets = 8;
constexpr uint32_t kDefaultDrawLimit = 1000;
constexpr uint32_t kDefaultShaderSearchLimit = 50;
constexpr uint32_t kHistogramBucketCount = 256;
constexpr uint32_t kMaxDebugVarComponents = 16;

} // namespace renderdoc::core
