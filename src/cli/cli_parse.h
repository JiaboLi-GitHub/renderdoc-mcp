#pragma once

#include "core/types.h"
#include <optional>
#include <string>
#include <vector>

namespace renderdoc::cli {

struct Args {
    std::string capturePath;
    std::string command;
    std::vector<std::string> positional;
    std::optional<uint32_t> eventId;
    std::string filter;
    std::string typeFilter;
    std::string outputDir;
    std::string workingDir;
    std::string cmdLineArgs;
    uint32_t delayFrames = 100;
    // Phase 1 additions
    uint32_t targetIndex = 0;
    uint32_t mipLevel = 0;
    uint32_t sliceIndex = 0;
    uint32_t instance = 0;
    uint32_t primitive = 0xFFFFFFFF;
    uint32_t index = 0xFFFFFFFF;
    uint32_t view = 0;
    bool trace = false;
    bool histogram = false;
    // Phase 2
    std::string encoding;
    std::string entry = "main";
    uint64_t withShaderId = 0;
    std::string format = "obj";
    std::string expectStr;
    float expectRGBA[4] = {};
    bool hasExpectRGBA = false;
    float tolerance = 0.01f;
    std::string opStr = "eq";
    int expectCount = 0;
    std::string minSeverity = "high";
    std::string diffOutput;
    double threshold = 0.0;
    std::string stageStr = "vs-out";
    // Phase 5: Counters + CBuffer
    bool listMode = false;
    std::string counterFilter;
    std::optional<uint32_t> cbufferIndex;
};

std::optional<renderdoc::core::ShaderStage> parseStage(const std::string& s);

// Numeric parse helpers — print error to stderr and call std::exit(1) on failure.
// For testability, provide throwing variants.
uint32_t parseUint32(const std::string& str, const std::string& flagName);
float parseFloat(const std::string& str, const std::string& flagName);
uint64_t parseUint64(const std::string& str, const std::string& flagName);
double parseDouble(const std::string& str, const std::string& flagName);

void printUsage(const char* argv0);
Args parseArgs(int argc, char* argv[]);

} // namespace renderdoc::cli
