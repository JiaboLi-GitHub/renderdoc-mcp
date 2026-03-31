#include <gtest/gtest.h>
#include "mcp/serialization.h"

using namespace renderdoc;

TEST(ResourceIdSerialization, RoundTrip) {
    EXPECT_EQ(mcp::resourceIdToString(123), "ResourceId::123");
    EXPECT_EQ(mcp::parseResourceId("ResourceId::123"), 123u);
    EXPECT_EQ(mcp::resourceIdToString(0), "ResourceId::0");
    EXPECT_EQ(mcp::parseResourceId("ResourceId::0"), 0u);
}

TEST(ResourceIdSerialization, InvalidFormat) {
    EXPECT_THROW(mcp::parseResourceId("123"), std::invalid_argument);
    EXPECT_THROW(mcp::parseResourceId("ResourceId:"), std::invalid_argument);
    EXPECT_THROW(mcp::parseResourceId(""), std::invalid_argument);
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
