#pragma once

#include <array>
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
    bool isRemote = false;
    std::string remoteUrl;
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

// --- Pixel Query ---
struct PixelValue {
    float floatValue[4] = {};
    uint32_t uintValue[4] = {};
    int32_t intValue[4] = {};
};

struct PixelModification {
    uint32_t eventId = 0;
    uint32_t fragmentIndex = 0;
    uint32_t primitiveId = 0;
    PixelValue shaderOut;
    PixelValue postMod;
    std::optional<float> depth;
    bool passed = false;
    std::vector<std::string> flags;
};

struct PixelHistoryResult {
    uint32_t x = 0, y = 0, eventId = 0;
    uint32_t targetIndex = 0;
    ResourceId targetId = 0;
    std::vector<PixelModification> modifications;
};

struct PickPixelResult {
    uint32_t x = 0, y = 0, eventId = 0;
    uint32_t targetIndex = 0;
    ResourceId targetId = 0;
    PixelValue color;
};

// --- Shader Debug ---
struct DebugVariable {
    std::string name;
    std::string type;       // VarType as string: "Float", "UInt", "SInt", "Bool", etc.
    uint32_t rows = 0;
    uint32_t cols = 0;
    uint32_t flags = 0;     // ShaderVariableFlags bitmask
    std::vector<float> floatValues;
    std::vector<uint32_t> uintValues;
    std::vector<int32_t> intValues;
    std::vector<DebugVariable> members;
};

struct DebugVariableChange {
    DebugVariable before;
    DebugVariable after;
};

struct DebugStep {
    uint32_t step = 0;
    uint32_t instruction = 0;
    std::string file;
    int32_t line = -1;
    std::vector<DebugVariableChange> changes;
};

struct ShaderDebugResult {
    uint32_t eventId = 0;
    std::string stage;
    uint32_t totalSteps = 0;
    std::vector<DebugVariable> inputs;
    std::vector<DebugVariable> outputs;
    std::vector<DebugStep> trace;
};

// --- Texture Stats ---
struct TextureStats {
    ResourceId id = 0;
    uint32_t eventId = 0;
    uint32_t mip = 0;
    uint32_t slice = 0;
    PixelValue minVal;
    PixelValue maxVal;
    struct HistogramBucket {
        uint32_t r = 0, g = 0, b = 0, a = 0;
    };
    std::vector<HistogramBucket> histogram;
};

// --- Shader Editing ---
enum class ShaderEncoding {
    Unknown = 0, DXBC = 1, GLSL = 2, SPIRV = 3,
    SPIRVAsm = 4, HLSL = 5, DXIL = 6,
    OpenGLSPIRV = 7, OpenGLSPIRVAsm = 8, Slang = 9
};

struct ShaderBuildResult {
    uint64_t shaderId = 0;   // 0 = failure
    std::string warnings;     // compiler warnings or error message
};

// --- Mesh Export ---
enum class MeshStage { VSOut = 1, GSOut = 2 };
enum class MeshTopology { TriangleList, TriangleStrip, TriangleFan, Other };

struct MeshVertex {
    float x = 0, y = 0, z = 0;
};

struct MeshData {
    uint32_t eventId = 0;
    MeshStage stage = MeshStage::VSOut;
    MeshTopology topology = MeshTopology::Other;
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<std::array<uint32_t, 3>> faces;
};

// --- Snapshot ---
struct SnapshotResult {
    std::string manifestPath;
    std::vector<std::string> files;
    std::vector<std::string> errors;
};

// --- Resource Usage ---
struct ResourceUsageEntry {
    uint32_t eventId = 0;
    std::string usage;
};

struct ResourceUsageResult {
    ResourceId resourceId = 0;
    std::vector<ResourceUsageEntry> entries;
};

// --- Assertions ---
// AssertResult uses std::map<string,string> for details to keep core layer
// free of nlohmann::json. The MCP serialization layer converts to JSON.
struct AssertResult {
    bool pass = false;
    std::string message;
    std::map<std::string, std::string> details;  // key-value pairs, all stringified
};

// Pixel assertion carries typed actual/expected values for precise serialization.
struct PixelAssertResult {
    bool pass = false;
    std::string message;
    float actual[4] = {};
    float expected[4] = {};
    float tolerance = 0.01f;
};

struct ImageCompareResult {
    bool pass = false;
    size_t diffPixels = 0;
    size_t totalPixels = 0;
    double diffRatio = 0.0;
    std::string diffOutputPath;
    std::string message;
};

// --- Phase 4: Pass Analysis ---

struct PassRange {
    std::string name;
    uint32_t beginEventId = 0;      // marker or first event in synthetic range
    uint32_t endEventId = 0;        // last event in range (inclusive)
    uint32_t firstDrawEventId = 0;  // first actual draw/dispatch inside the range
    bool synthetic = false;
};

struct AttachmentInfo {
    ResourceId resourceId = 0;
    std::string name;
    std::string format;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct PassAttachments {
    std::string passName;
    uint32_t eventId = 0;
    std::vector<AttachmentInfo> colorTargets;
    AttachmentInfo depthTarget;
    bool hasDepth = false;
    bool synthetic = false;
};

struct PassStatistics {
    std::string name;
    uint32_t eventId = 0;
    uint32_t drawCount = 0;
    uint32_t dispatchCount = 0;
    uint64_t totalTriangles = 0;
    uint32_t rtWidth = 0;
    uint32_t rtHeight = 0;
    uint32_t attachmentCount = 0;
    bool synthetic = false;
};

struct PassEdge {
    std::string srcPass;
    std::string dstPass;
    std::vector<ResourceId> sharedResources;
};

struct PassDependencyGraph {
    std::vector<PassEdge> edges;
    uint32_t passCount = 0;
    uint32_t edgeCount = 0;
};

struct UnusedTarget {
    ResourceId resourceId = 0;
    std::string name;
    std::vector<std::string> writtenBy;
    uint32_t wave = 0;
};

struct UnusedTargetResult {
    std::vector<UnusedTarget> unused;
    uint32_t unusedCount = 0;
    uint32_t totalTargets = 0;
};

// --- GPU Performance Counters ---

struct CounterInfo {
    uint32_t id = 0;           // GPUCounter enum value
    std::string name;
    std::string category;
    std::string description;
    std::string resultType;    // "Float", "UInt", "Double", etc.
    uint32_t resultByteWidth = 0;
    std::string unit;          // "Seconds", "Bytes", "Percentage", etc.
};

struct CounterSample {
    uint32_t eventId = 0;
    uint32_t counterId = 0;
    std::string counterName;
    double value = 0.0;        // all types unified to double
    std::string unit;
};

struct CounterFetchResult {
    std::vector<CounterSample> rows;
    uint32_t totalCounters = 0;
    uint32_t totalEvents = 0;
};

// --- CBuffer Contents ---

struct ShaderVar {
    std::string name;
    std::string typeName;      // "float", "float4x4", "int", "struct", etc.
    uint8_t rows = 0;
    uint8_t columns = 0;
    std::vector<double> floatValues;
    std::vector<int64_t> intValues;
    std::vector<uint64_t> uintValues;
    std::vector<ShaderVar> members;
};

struct CBufferInfo {
    uint32_t index = 0;
    std::string name;
    uint32_t bindSet = 0;      // Vulkan set / DX space
    uint32_t bindSlot = 0;     // binding / register
    uint32_t byteSize = 0;
    bool bufferBacked = true;
    uint32_t variableCount = 0;
};

struct CBufferContents {
    uint32_t eventId = 0;
    ShaderStage stage = ShaderStage::Vertex;
    uint32_t bindSet = 0;
    uint32_t bindSlot = 0;
    std::string blockName;
    uint32_t byteSize = 0;
    std::vector<ShaderVar> variables;
};

} // namespace renderdoc::core
