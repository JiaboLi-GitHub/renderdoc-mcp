#include "core/assertions.h"
#include "core/errors.h"
#include "core/events.h"
#include "core/info.h"
#include "core/pipeline.h"
#include "core/pixel.h"
#include "core/resources.h"
#include "core/session.h"

#include <cmath>
#include <memory>
#include <sstream>

#include "stb_image.h"
#include "stb_image_write.h"

#include <renderdoc_replay.h>

namespace renderdoc::core {

namespace {
struct StbiImageDeleter {
    void operator()(unsigned char* p) const { if (p) stbi_image_free(p); }
};
using StbiImage = std::unique_ptr<unsigned char[], StbiImageDeleter>;
} // anonymous namespace

// --- assert_pixel ---
PixelAssertResult assertPixel(const Session& session,
                              uint32_t eventId,
                              uint32_t x, uint32_t y,
                              const float expected[4],
                              float tolerance,
                              uint32_t target) {
    auto pixel = pickPixel(session, x, y, target, eventId);

    PixelAssertResult result;
    result.tolerance = tolerance;
    for (int i = 0; i < 4; ++i) {
        result.actual[i] = pixel.color.floatValue[i];
        result.expected[i] = expected[i];
    }

    bool allMatch = true;
    for (int i = 0; i < 4; ++i) {
        if (std::isnan(result.actual[i]) || std::isnan(result.expected[i]) ||
            std::isinf(result.actual[i]) || std::isinf(result.expected[i]) ||
            std::fabs(result.actual[i] - result.expected[i]) > tolerance) {
            allMatch = false;
            break;
        }
    }

    result.pass = allMatch;
    if (allMatch) {
        result.message = "pixel (" + std::to_string(x) + ", " + std::to_string(y) +
                         ") matches expected value within tolerance";
    } else {
        std::ostringstream ss;
        ss << "pixel (" << x << ", " << y << ") expected ["
           << expected[0] << ", " << expected[1] << ", "
           << expected[2] << ", " << expected[3] << "], got ["
           << result.actual[0] << ", " << result.actual[1] << ", "
           << result.actual[2] << ", " << result.actual[3] << "]";
        result.message = ss.str();
    }
    return result;
}

// --- assert_state ---
AssertResult assertState(const std::string& path,
                         const std::string& actual,
                         const std::string& expected) {
    AssertResult result;
    result.pass = (actual == expected);
    result.details["actual"] = actual;
    result.details["expected"] = expected;
    result.details["path"] = path;
    if (result.pass) {
        result.message = path + " = " + actual;
    } else {
        result.message = path + " = " + actual + " (expected " + expected + ")";
    }
    return result;
}

// --- assert_image ---
ImageCompareResult assertImage(const std::string& expectedPath,
                               const std::string& actualPath,
                               double threshold,
                               const std::string& diffOutputPath) {
    int ew, eh, ec, aw, ah, ac;
    StbiImage expectedData(stbi_load(expectedPath.c_str(), &ew, &eh, &ec, 4));
    if (!expectedData)
        throw CoreError(CoreError::Code::ImageLoadFailed,
                        "Failed to load expected image: " + expectedPath);

    StbiImage actualData(stbi_load(actualPath.c_str(), &aw, &ah, &ac, 4));
    if (!actualData)
        throw CoreError(CoreError::Code::ImageLoadFailed,
                        "Failed to load actual image: " + actualPath);

    if (ew != aw || eh != ah)
        throw CoreError(CoreError::Code::ImageSizeMismatch,
                        "Image size mismatch: " + std::to_string(ew) + "x" + std::to_string(eh) +
                        " vs " + std::to_string(aw) + "x" + std::to_string(ah));

    size_t totalPixels = static_cast<size_t>(ew) * static_cast<size_t>(eh);
    size_t diffPixels = 0;
    std::vector<unsigned char> diffImage;
    if (!diffOutputPath.empty()) diffImage.resize(totalPixels * 4);

    for (size_t i = 0; i < totalPixels; ++i) {
        size_t offset = i * 4;
        bool same = (expectedData[offset] == actualData[offset] &&
                     expectedData[offset+1] == actualData[offset+1] &&
                     expectedData[offset+2] == actualData[offset+2] &&
                     expectedData[offset+3] == actualData[offset+3]);
        if (!same) diffPixels++;
        if (!diffOutputPath.empty()) {
            if (!same) {
                diffImage[offset] = 255; diffImage[offset+1] = 0;
                diffImage[offset+2] = 0; diffImage[offset+3] = 255;
            } else {
                uint8_t gray = static_cast<uint8_t>(
                    0.299f * expectedData[offset] + 0.587f * expectedData[offset+1] +
                    0.114f * expectedData[offset+2]);
                diffImage[offset] = gray; diffImage[offset+1] = gray;
                diffImage[offset+2] = gray; diffImage[offset+3] = expectedData[offset+3];
            }
        }
    }

    // RAII handles stbi_image_free automatically
    expectedData.reset();
    actualData.reset();
    if (!diffOutputPath.empty() && diffPixels > 0) {
        if (!stbi_write_png(diffOutputPath.c_str(), ew, eh, 4, diffImage.data(), ew * 4))
            throw CoreError(CoreError::Code::ExportFailed,
                            "Failed to write diff image: " + diffOutputPath);
    }

    double diffRatio = (totalPixels > 0) ? (static_cast<double>(diffPixels) / totalPixels * 100.0) : 0.0;

    ImageCompareResult result;
    result.pass = (diffRatio <= threshold);
    result.diffPixels = diffPixels;
    result.totalPixels = totalPixels;
    result.diffRatio = diffRatio;
    result.diffOutputPath = diffOutputPath;
    result.message = result.pass
        ? "images match (diff ratio: " + std::to_string(diffRatio) + "%)"
        : "images differ: " + std::to_string(diffPixels) + "/" + std::to_string(totalPixels) +
          " pixels (" + std::to_string(diffRatio) + "%)";
    return result;
}

// --- assert_count ---
AssertResult assertCount(const Session& session,
                         const std::string& what,
                         int expected,
                         const std::string& op) {
    auto* ctrl = session.controller();
    int actual = 0;
    if (what == "events") {
        auto st = session.status();
        actual = static_cast<int>(st.totalEvents);
    } else if (what == "draws") {
        auto draws = listDraws(session, "", UINT32_MAX);
        actual = static_cast<int>(draws.size());
    } else if (what == "textures") {
        auto textures = ctrl->GetTextures();
        actual = static_cast<int>(textures.size());
    } else if (what == "buffers") {
        auto buffers = ctrl->GetBuffers();
        actual = static_cast<int>(buffers.size());
    } else if (what == "passes") {
        auto passes = listPasses(session);
        actual = static_cast<int>(passes.size());
    } else {
        throw CoreError(CoreError::Code::InternalError,
                        "Unknown count target: " + what);
    }

    bool pass = false;
    if (op == "eq") pass = (actual == expected);
    else if (op == "gt") pass = (actual > expected);
    else if (op == "lt") pass = (actual < expected);
    else if (op == "ge") pass = (actual >= expected);
    else if (op == "le") pass = (actual <= expected);
    else throw CoreError(CoreError::Code::InternalError, "Unknown operator: " + op);

    AssertResult result;
    result.pass = pass;
    result.details["actual"] = std::to_string(actual);
    result.details["expected"] = std::to_string(expected);
    result.details["op"] = op;
    result.details["what"] = what;
    result.message = what + " = " + std::to_string(actual) +
                     " (expected " + op + " " + std::to_string(expected) + ")";
    return result;
}

// --- assert_clean ---
CleanAssertResult assertClean(const Session& session,
                              const std::string& minSeverity) {
    auto messages = getLog(session, minSeverity);
    CleanAssertResult out;
    out.result.pass = messages.empty();
    out.result.details["count"] = std::to_string(messages.size());
    out.result.details["minSeverity"] = minSeverity;
    out.messages = std::move(messages);
    out.result.message = out.result.pass
        ? "no messages at severity >= " + minSeverity
        : std::to_string(out.messages.size()) + " message(s) at severity >= " + minSeverity;
    return out;
}

} // namespace renderdoc::core
