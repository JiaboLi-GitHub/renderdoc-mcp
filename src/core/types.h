#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace renderdoc::core {

// --- Common ---
using ResourceId = uint64_t;

enum class GraphicsApi { D3D11, D3D12, OpenGL, Vulkan, Unknown };
enum class ShaderStage { Vertex, Hull, Domain, Geometry, Pixel, Compute };

// ActionFlags: raw RenderDoc bitmask passthrough. The MCP serializer
// converts to pipe-separated strings ("Drawcall|Indexed|Instanced").
using ActionFlagBits = uint32_t;

// --- Session ---
struct CaptureInfo {
    std::string path;
    GraphicsApi api;
    bool degraded = false;
    uint32_t totalEvents = 0;
    uint32_t totalDraws = 0;
    std::string machineIdent;
    std::string driverName;
    bool hasCallstacks = false;
    uint64_t timestampBase = 0;
    struct GpuInfo {
        std::string name;
        std::string vendor;
        uint32_t deviceID = 0;
        std::string driver;
    };
    std::vector<GpuInfo> gpus;
};

struct SessionStatus {
    bool isOpen = false;
    std::string capturePath;
    GraphicsApi api = GraphicsApi::Unknown;
    uint32_t currentEventId = 0;
    uint32_t totalEvents = 0;
};

// --- Events ---
struct EventInfo {
    uint32_t eventId = 0;
    std::string name;
    ActionFlagBits flags = 0;
    uint32_t numIndices = 0;
    uint32_t numInstances = 0;
    uint32_t drawIndex = 0;
    std::vector<ResourceId> outputs;
};

// --- Pipeline ---
struct ShaderBindingDetail {
    std::string name;
    uint32_t bindPoint = 0;
    uint32_t byteSize = 0;
    uint32_t variableCount = 0;
};

struct StageBindings {
    ResourceId shaderId = 0;
    std::vector<ShaderBindingDetail> constantBuffers;
    std::vector<ShaderBindingDetail> readOnlyResources;
    std::vector<ShaderBindingDetail> readWriteResources;
    std::vector<ShaderBindingDetail> samplers;
};

struct BoundResource {
    ResourceId id = 0;
    std::string name;
    std::string typeName;
    uint32_t bindPoint = 0;
};

struct RenderTargetInfo {
    ResourceId id = 0;
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string format;
};

struct Viewport {
    float x = 0, y = 0, width = 0, height = 0, minDepth = 0, maxDepth = 0;
};

struct PipelineState {
    GraphicsApi api = GraphicsApi::Unknown;
    struct ShaderBinding {
        ShaderStage stage = ShaderStage::Vertex;
        ResourceId shaderId = 0;
        std::string entryPoint;
    };
    std::vector<ShaderBinding> shaders;
    std::vector<RenderTargetInfo> renderTargets;
    std::optional<RenderTargetInfo> depthTarget;
    std::vector<Viewport> viewports;
};

// --- Resources ---
struct ResourceInfo {
    ResourceId id = 0;
    std::string name;
    std::string type;
    uint64_t byteSize = 0;
    std::optional<uint32_t> width;
    std::optional<uint32_t> height;
    std::optional<uint32_t> depth;
    std::optional<uint32_t> mips;
    std::optional<uint32_t> arraySize;
    std::optional<std::string> format;
    std::optional<std::string> dimension;
    std::optional<bool> cubemap;
    std::optional<uint32_t> msSamp;
    struct FormatDetails {
        std::string name;
        uint32_t compCount = 0;
        uint32_t compByteWidth = 0;
        uint32_t compType = 0;
    };
    std::optional<FormatDetails> formatDetails;
    std::optional<uint64_t> gpuAddress;
};

// --- Passes ---
struct PassInfo {
    std::string name;
    uint32_t eventId = 0;
    uint32_t drawCount = 0;
    uint32_t dispatchCount = 0;
    std::vector<EventInfo> draws;
};

// --- Info/Stats ---
struct DebugMessage {
    uint32_t eventId = 0;
    std::string severity;
    std::string category;
    std::string message;
};

struct PerPassStats {
    std::string name;
    uint32_t drawCount = 0;
    uint32_t dispatchCount = 0;
    uint64_t totalTriangles = 0;
};

struct TopDraw {
    uint32_t eventId = 0;
    std::string name;
    uint32_t numIndices = 0;
};

struct LargestResource {
    std::string name;
    uint64_t byteSize = 0;
    std::string type;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct CaptureStats {
    std::vector<PerPassStats> perPass;
    std::vector<TopDraw> topDraws;
    std::vector<LargestResource> largestResources;
};

// --- Shaders ---
struct SignatureElement {
    std::string varName;
    std::string semanticName;
    uint32_t semanticIndex = 0;
    uint32_t regIndex = 0;
};

struct ConstantBlock {
    std::string name;
    uint32_t bindPoint = 0;
    uint32_t byteSize = 0;
    uint32_t variableCount = 0;
};

struct ShaderReflection {
    ResourceId id = 0;
    ShaderStage stage = ShaderStage::Vertex;
    std::string entryPoint;
    std::vector<SignatureElement> inputSignature;
    std::vector<SignatureElement> outputSignature;
    std::vector<ConstantBlock> constantBlocks;
    std::vector<ShaderBindingDetail> readOnlyResources;
    std::vector<ShaderBindingDetail> readWriteResources;
};

struct ShaderDisassembly {
    ResourceId id = 0;
    ShaderStage stage = ShaderStage::Vertex;
    std::string disassembly;
    std::string target;
};

struct ShaderUsageInfo {
    ResourceId shaderId = 0;
    ShaderStage stage = ShaderStage::Vertex;
    std::string entryPoint;
    uint32_t usageCount = 0;
};

struct ShaderSearchMatch {
    ResourceId shaderId = 0;
    ShaderStage stage = ShaderStage::Vertex;
    std::string entryPoint;
    struct MatchLine {
        uint32_t line = 0;
        std::string text;
    };
    std::vector<MatchLine> matchingLines;
};

// --- Export ---
struct ExportResult {
    std::string outputPath;
    uint64_t byteSize = 0;
    uint32_t eventId = 0;
    int rtIndex = -1;
    uint32_t width = 0;
    uint32_t height = 0;
    ResourceId resourceId = 0;
    uint32_t mip = 0;
    uint32_t layer = 0;
    uint64_t offset = 0;
    uint64_t requestedSize = 0;
};

// --- Capture ---
struct CaptureRequest {
    std::string exePath;
    std::string workingDir;
    std::string cmdLine;
    uint32_t delayFrames = 100;
    std::string outputPath;
};

struct CaptureResult {
    std::string capturePath;
    uint32_t pid = 0;
};

} // namespace renderdoc::core
