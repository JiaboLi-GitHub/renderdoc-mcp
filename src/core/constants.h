#pragma once
#include <cstdint>

namespace renderdoc::core {

// D3D11/12 maximum simultaneous render targets; Vulkan guarantees at least 4,
// but 8 matches the D3D limit and covers all common hardware.
constexpr int kMaxRenderTargets = 8;

// Default page size for list_draws tool output.
// Project choice: balances detail vs. MCP response size for AI consumption.
constexpr uint32_t kDefaultDrawLimit = 1000;

// Default max results for search_shaders tool.
// Project choice: keeps response size manageable while covering typical captures.
constexpr uint32_t kDefaultShaderSearchLimit = 50;

// Number of histogram buckets for get_texture_stats tool.
// Matches 8-bit value range (0-255), the standard resolution for texture histograms.
constexpr uint32_t kHistogramBucketCount = 256;

// Maximum float/uint/int components stored per shader debug variable.
// Covers mat4 (16 floats), the largest common HLSL/GLSL type.
constexpr uint32_t kMaxDebugVarComponents = 16;

} // namespace renderdoc::core
