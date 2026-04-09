#pragma once

#include "core/types.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace renderdoc::core {

class Session;

PixelAssertResult assertPixel(const Session& session,
                              uint32_t eventId,
                              uint32_t x, uint32_t y,
                              const float expected[4],
                              float tolerance = 0.01f,
                              uint32_t target = 0);

AssertResult assertState(const std::string& path,
                         const std::string& actual,
                         const std::string& expected);

ImageCompareResult assertImage(const std::string& expectedPath,
                               const std::string& actualPath,
                               double threshold = 0.0,
                               const std::string& diffOutputPath = "");

AssertResult assertCount(const Session& session,
                         const std::string& what,
                         int64_t expected,
                         const std::string& op = "eq");

struct CleanAssertResult {
    AssertResult result;
    std::vector<DebugMessage> messages;
};

CleanAssertResult assertClean(const Session& session,
                              const std::string& minSeverity = "high");

} // namespace renderdoc::core
