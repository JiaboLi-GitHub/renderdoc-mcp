#include <gtest/gtest.h>
#include "mcp/serialization.h"
#include "core/errors.h"

using namespace renderdoc;

TEST(ResourceIdSerialization, RoundTrip) {
    EXPECT_EQ(mcp::resourceIdToString(123), "ResourceId::123");
    EXPECT_EQ(mcp::parseResourceId("ResourceId::123"), 123u);
    EXPECT_EQ(mcp::resourceIdToString(0), "ResourceId::0");
    EXPECT_EQ(mcp::parseResourceId("ResourceId::0"), 0u);
}

TEST(ResourceIdSerialization, InvalidFormat) {
    EXPECT_THROW(mcp::parseResourceId("123"), core::CoreError);
    EXPECT_THROW(mcp::parseResourceId("ResourceId:"), core::CoreError);
    EXPECT_THROW(mcp::parseResourceId(""), core::CoreError);
}

TEST(ResourceIdSerialization, InvalidNumber) {
    EXPECT_THROW(mcp::parseResourceId("ResourceId::abc"), core::CoreError);
    EXPECT_THROW(mcp::parseResourceId("ResourceId::-1"), core::CoreError);
    EXPECT_THROW(mcp::parseResourceId("ResourceId::"), core::CoreError);
}

// NOTE: actionFlagsToString uses RenderDoc's ActionFlags enum and lives in
// renderdoc-mcp-lib, so it can only be tested in integration tests (test-tools),
// not in unit tests (test-unit which links renderdoc-mcp-proto only).
// The unit test here verifies the other serialization helpers instead.

TEST(GraphicsApiSerialization, AllValues) {
    EXPECT_EQ(mcp::graphicsApiToString(core::GraphicsApi::D3D11), "D3D11");
    EXPECT_EQ(mcp::graphicsApiToString(core::GraphicsApi::Vulkan), "Vulkan");
    EXPECT_EQ(mcp::graphicsApiToString(core::GraphicsApi::Unknown), "Unknown");
}

TEST(ShaderStageSerialization, RoundTrip) {
    EXPECT_EQ(mcp::shaderStageToString(core::ShaderStage::Vertex), "vs");
    EXPECT_EQ(mcp::shaderStageToString(core::ShaderStage::Pixel), "ps");
    EXPECT_EQ(mcp::parseShaderStage("vs"), core::ShaderStage::Vertex);
    EXPECT_EQ(mcp::parseShaderStage("cs"), core::ShaderStage::Compute);
    EXPECT_THROW(mcp::parseShaderStage("invalid"), std::invalid_argument);
}

TEST(CaptureInfoSerialization, BasicFields) {
    core::CaptureInfo info;
    info.path = "test.rdc";
    info.api = core::GraphicsApi::Vulkan;
    info.totalEvents = 100;
    info.totalDraws = 10;
    auto j = mcp::to_json(info);
    EXPECT_EQ(j["path"], "test.rdc");
    EXPECT_EQ(j["api"], "Vulkan");
    EXPECT_EQ(j["totalEvents"], 100);
    EXPECT_EQ(j["totalDraws"], 10);
    EXPECT_TRUE(j["gpus"].is_array());
}

TEST(EventInfoSerialization, WithOutputs) {
    core::EventInfo e;
    e.eventId = 42;
    e.name = "vkCmdDraw";
    e.flags = 0x0002 | 0x10000;  // Drawcall|Indexed
    e.numIndices = 36;
    e.outputs = {111, 222};
    auto j = mcp::to_json(e);
    EXPECT_EQ(j["eventId"], 42);
    EXPECT_EQ(j["flags"], "Drawcall|Indexed");
    EXPECT_EQ(j["outputs"][0], "ResourceId::111");
    EXPECT_EQ(j["outputs"][1], "ResourceId::222");
}

TEST(ResourceInfoSerialization, TextureFields) {
    core::ResourceInfo r;
    r.id = 5;
    r.name = "albedo";
    r.type = "Texture2D";
    r.byteSize = 1024;
    r.width = 512;
    r.height = 512;
    r.format = "R8G8B8A8_UNORM";
    auto j = mcp::to_json(r);
    EXPECT_EQ(j["resourceId"], "ResourceId::5");
    EXPECT_EQ(j["type"], "Texture2D");
    EXPECT_EQ(j["width"], 512);
    EXPECT_EQ(j["format"], "R8G8B8A8_UNORM");
    EXPECT_FALSE(j.contains("gpuAddress"));
}

TEST(ResourceInfoSerialization, BufferFields) {
    core::ResourceInfo r;
    r.id = 88;
    r.name = "verts";
    r.type = "Buffer";
    r.byteSize = 4096;
    r.gpuAddress = 0xDEADBEEF;
    auto j = mcp::to_json(r);
    EXPECT_EQ(j["type"], "Buffer");
    EXPECT_FALSE(j.contains("width"));
    EXPECT_EQ(j["gpuAddress"], 0xDEADBEEF);
}

TEST(ExportResultSerialization, RenderTarget) {
    core::ExportResult e;
    e.outputPath = "/tmp/rt.png";
    e.byteSize = 2048;
    e.eventId = 11;
    e.rtIndex = 0;
    e.width = 500;
    e.height = 500;
    auto j = mcp::to_json(e);
    EXPECT_EQ(j["path"], "/tmp/rt.png");
    EXPECT_EQ(j["rtIndex"], 0);
    EXPECT_EQ(j["width"], 500);
    EXPECT_FALSE(j.contains("resourceId"));  // no resourceId for RT export
}

TEST(CaptureResultSerialization, BasicFields) {
    core::CaptureResult result;
    result.capturePath = "C:/tmp/test_capture.rdc";
    result.pid = 12345;

    auto j = mcp::to_json(result);
    EXPECT_EQ(j["capturePath"], "C:/tmp/test_capture.rdc");
    EXPECT_EQ(j["pid"], 12345u);
}

// --- Phase 1 serialization tests ---

TEST(PixelValueSerialization, AllBranches) {
    core::PixelValue pv;
    pv.floatValue[0] = 0.5f; pv.floatValue[1] = 0.25f;
    pv.floatValue[2] = 0.125f; pv.floatValue[3] = 1.0f;
    pv.uintValue[0] = 128; pv.uintValue[1] = 64;
    pv.uintValue[2] = 32; pv.uintValue[3] = 255;
    pv.intValue[0] = -1; pv.intValue[1] = -2;
    pv.intValue[2] = -3; pv.intValue[3] = 0;

    auto j = mcp::to_json(pv);
    EXPECT_TRUE(j.contains("floatValue"));
    EXPECT_TRUE(j.contains("uintValue"));
    EXPECT_TRUE(j.contains("intValue"));
    EXPECT_EQ(j["floatValue"].size(), 4u);
    EXPECT_FLOAT_EQ(j["floatValue"][0].get<float>(), 0.5f);
    EXPECT_EQ(j["uintValue"][0], 128u);
    EXPECT_EQ(j["intValue"][0], -1);
}

TEST(PixelModificationSerialization, WithDepth) {
    core::PixelModification mod;
    mod.eventId = 42;
    mod.fragmentIndex = 0;
    mod.primitiveId = 7;
    mod.depth = 0.95f;
    mod.passed = true;
    mod.flags = {"depthTestFailed", "scissorClipped"};

    auto j = mcp::to_json(mod);
    EXPECT_EQ(j["eventId"], 42);
    EXPECT_EQ(j["primitiveId"], 7);
    EXPECT_FLOAT_EQ(j["depth"].get<float>(), 0.95f);
    EXPECT_TRUE(j["passed"].get<bool>());
    EXPECT_EQ(j["flags"].size(), 2u);
}

TEST(PixelModificationSerialization, NullDepth) {
    core::PixelModification mod;
    mod.depth = std::nullopt;

    auto j = mcp::to_json(mod);
    EXPECT_TRUE(j["depth"].is_null());
}

TEST(PixelHistoryResultSerialization, BasicFields) {
    core::PixelHistoryResult result;
    result.x = 10; result.y = 20;
    result.eventId = 50;
    result.targetIndex = 0;
    result.targetId = 123;

    auto j = mcp::to_json(result);
    EXPECT_EQ(j["x"], 10);
    EXPECT_EQ(j["y"], 20);
    EXPECT_EQ(j["eventId"], 50);
    EXPECT_EQ(j["targetId"], "ResourceId::123");
    EXPECT_TRUE(j["modifications"].is_array());
}

TEST(PickPixelResultSerialization, BasicFields) {
    core::PickPixelResult result;
    result.x = 5; result.y = 15;
    result.eventId = 30;
    result.targetIndex = 1;
    result.targetId = 456;

    auto j = mcp::to_json(result);
    EXPECT_EQ(j["x"], 5);
    EXPECT_EQ(j["targetIndex"], 1);
    EXPECT_TRUE(j.contains("color"));
    EXPECT_TRUE(j["color"].contains("floatValue"));
}

TEST(DebugVariableSerialization, FloatType) {
    core::DebugVariable var;
    var.name = "position";
    var.type = "Float";
    var.rows = 1; var.cols = 4;
    var.flags = 0;
    var.floatValues = {1.0f, 2.0f, 3.0f, 1.0f};

    auto j = mcp::to_json(var);
    EXPECT_EQ(j["name"], "position");
    EXPECT_EQ(j["type"], "Float");
    EXPECT_EQ(j["rows"], 1);
    EXPECT_EQ(j["cols"], 4);
    EXPECT_EQ(j["floatValues"].size(), 4u);
    EXPECT_TRUE(j["uintValues"].is_array());
    EXPECT_EQ(j["uintValues"].size(), 0u);
    EXPECT_TRUE(j["members"].is_array());
    EXPECT_EQ(j["members"].size(), 0u);
}

TEST(DebugVariableSerialization, WithMembers) {
    core::DebugVariable parent;
    parent.name = "cbuffer";
    parent.type = "Struct";
    parent.rows = 0; parent.cols = 0;

    core::DebugVariable child;
    child.name = "color";
    child.type = "Float";
    child.rows = 1; child.cols = 4;
    child.floatValues = {1.0f, 0.0f, 0.0f, 1.0f};
    parent.members.push_back(child);

    auto j = mcp::to_json(parent);
    EXPECT_EQ(j["members"].size(), 1u);
    EXPECT_EQ(j["members"][0]["name"], "color");
    EXPECT_EQ(j["members"][0]["floatValues"].size(), 4u);
}

TEST(DebugVariableChangeSerialization, BeforeAfter) {
    core::DebugVariableChange change;
    change.before.name = "x";
    change.before.type = "Float";
    change.before.floatValues = {0.0f};
    change.after.name = "x";
    change.after.type = "Float";
    change.after.floatValues = {1.0f};

    auto j = mcp::to_json(change);
    EXPECT_TRUE(j.contains("before"));
    EXPECT_TRUE(j.contains("after"));
    EXPECT_EQ(j["before"]["floatValues"][0], 0.0f);
    EXPECT_EQ(j["after"]["floatValues"][0], 1.0f);
}

TEST(ShaderDebugResultSerialization, SummaryMode) {
    core::ShaderDebugResult result;
    result.eventId = 100;
    result.stage = "ps";
    result.totalSteps = 42;

    core::DebugVariable input;
    input.name = "texcoord";
    input.type = "Float";
    input.rows = 1; input.cols = 2;
    input.floatValues = {0.5f, 0.5f};
    result.inputs.push_back(input);

    auto j = mcp::to_json(result);
    EXPECT_EQ(j["eventId"], 100);
    EXPECT_EQ(j["stage"], "ps");
    EXPECT_EQ(j["totalSteps"], 42);
    EXPECT_EQ(j["inputs"].size(), 1u);
    EXPECT_FALSE(j.contains("trace"));
}

TEST(ShaderDebugResultSerialization, WithTrace) {
    core::ShaderDebugResult result;
    result.eventId = 100;
    result.stage = "vs";
    result.totalSteps = 2;

    core::DebugStep step;
    step.step = 0;
    step.instruction = 5;
    step.file = "0";
    step.line = 10;
    result.trace.push_back(step);

    auto j = mcp::to_json(result);
    EXPECT_TRUE(j.contains("trace"));
    EXPECT_EQ(j["trace"].size(), 1u);
    EXPECT_EQ(j["trace"][0]["instruction"], 5);
    EXPECT_EQ(j["trace"][0]["line"], 10);
}

TEST(TextureStatsSerialization, WithoutHistogram) {
    core::TextureStats stats;
    stats.id = 99;
    stats.eventId = 50;
    stats.mip = 0;
    stats.slice = 0;
    stats.minVal.floatValue[0] = 0.0f;
    stats.maxVal.floatValue[0] = 1.0f;

    auto j = mcp::to_json(stats);
    EXPECT_EQ(j["id"], "ResourceId::99");
    EXPECT_EQ(j["eventId"], 50);
    EXPECT_TRUE(j.contains("min"));
    EXPECT_TRUE(j.contains("max"));
    EXPECT_FALSE(j.contains("histogram"));
}

TEST(TextureStatsSerialization, WithHistogram) {
    core::TextureStats stats;
    stats.id = 99;
    stats.eventId = 50;

    stats.histogram.resize(256);
    stats.histogram[0] = {10, 20, 30, 40};
    stats.histogram[255] = {1, 2, 3, 4};

    auto j = mcp::to_json(stats);
    EXPECT_TRUE(j.contains("histogram"));
    EXPECT_EQ(j["histogram"].size(), 256u);
    EXPECT_EQ(j["histogram"][0]["r"], 10);
    EXPECT_EQ(j["histogram"][255]["a"], 4);
}
