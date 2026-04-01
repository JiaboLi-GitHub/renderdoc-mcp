#include <gtest/gtest.h>
#include "mcp/tool_registry.h"
#include "mcp/tools/tools.h"
#include "core/session.h"

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;
using renderdoc::core::Session;
using renderdoc::mcp::ToolRegistry;
namespace tools = renderdoc::mcp::tools;

// Helper: try opening the capture in an SEH-guarded block so that headless
// machines without a GPU driver don't crash the entire test suite.
// Uses distinct names (openCaptureImpl2 / doOpenCaptureSEH2) to avoid
// linker conflicts with the identical helpers in test_tools.cpp.
#ifdef _WIN32
static void openCaptureImpl2(Session* s);

#pragma warning(push)
#pragma warning(disable: 4611)  // setjmp interaction
static bool doOpenCaptureSEH2(Session* s)
{
    __try
    {
        openCaptureImpl2(s);
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
#pragma warning(pop)

static void openCaptureImpl2(Session* s)
{
    s->open(TEST_RDC_PATH);
}
#endif

class Phase1ToolTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        tools::registerSessionTools(s_registry);
        tools::registerEventTools(s_registry);
        tools::registerPipelineTools(s_registry);
        tools::registerExportTools(s_registry);
        tools::registerInfoTools(s_registry);
        tools::registerResourceTools(s_registry);
        tools::registerShaderTools(s_registry);
        tools::registerCaptureTools(s_registry);
        tools::registerPixelTools(s_registry);
        tools::registerDebugTools(s_registry);
        tools::registerTexStatsTools(s_registry);

        // Open test capture
#ifdef _WIN32
        if (!doOpenCaptureSEH2(&s_session))
        {
            s_skipAll = true;
            return;
        }
#else
        s_session.open(TEST_RDC_PATH);
#endif
        ASSERT_TRUE(s_session.isOpen()) << "open_capture failed";

        // Navigate to first draw call
        auto draws = s_registry.callTool("list_draws", s_session, {});
        ASSERT_TRUE(draws.contains("draws"));
        ASSERT_GT(draws["draws"].size(), 0u);
        s_firstDrawEid = draws["draws"][0]["eventId"].get<uint32_t>();
        s_registry.callTool("goto_event", s_session, {{"eventId", s_firstDrawEid}});

        // Find a texture resource for tex-stats tests.
        // list_resources returns {"resources": [...], "count": N}
        auto resources = s_registry.callTool("list_resources", s_session, {{"type", "Texture"}});
        if (resources.contains("resources") && resources["resources"].is_array()
            && resources["resources"].size() > 0)
        {
            s_textureResId = resources["resources"][0]["resourceId"].get<std::string>();
        }
    }

    static void TearDownTestSuite() { s_session.close(); }

    void SetUp() override {
        if (s_skipAll)
            GTEST_SKIP() << "RenderDoc replay not available (no GPU or driver on this machine)";
    }

    static Session s_session;
    static ToolRegistry s_registry;
    static uint32_t s_firstDrawEid;
    static std::string s_textureResId;
    static bool s_skipAll;
};

Session Phase1ToolTest::s_session;
ToolRegistry Phase1ToolTest::s_registry;
uint32_t Phase1ToolTest::s_firstDrawEid = 0;
std::string Phase1ToolTest::s_textureResId;
bool Phase1ToolTest::s_skipAll = false;

// -- pick_pixel ---------------------------------------------------------------

TEST_F(Phase1ToolTest, PickPixel_ReturnsColor)
{
    auto result = s_registry.callTool("pick_pixel", s_session,
        {{"x", 0}, {"y", 0}});
    EXPECT_TRUE(result.contains("color"));
    EXPECT_TRUE(result["color"].contains("floatValue"));
    EXPECT_TRUE(result["color"].contains("uintValue"));
    EXPECT_TRUE(result["color"].contains("intValue"));
    EXPECT_EQ(result["color"]["floatValue"].size(), 4u);
    EXPECT_EQ(result["x"], 0);
    EXPECT_EQ(result["y"], 0);
}

TEST_F(Phase1ToolTest, PickPixel_WithEventId)
{
    auto result = s_registry.callTool("pick_pixel", s_session,
        {{"x", 0}, {"y", 0}, {"eventId", s_firstDrawEid}});
    EXPECT_EQ(result["eventId"], s_firstDrawEid);
}

TEST_F(Phase1ToolTest, PickPixel_OutOfBounds_Throws)
{
    EXPECT_THROW(
        s_registry.callTool("pick_pixel", s_session,
            {{"x", 99999}, {"y", 99999}}),
        std::exception);
}

TEST_F(Phase1ToolTest, PickPixel_InvalidTarget_Throws)
{
    EXPECT_THROW(
        s_registry.callTool("pick_pixel", s_session,
            {{"x", 0}, {"y", 0}, {"targetIndex", 99}}),
        std::exception);
}

// -- pixel_history ------------------------------------------------------------

TEST_F(Phase1ToolTest, PixelHistory_ReturnsModifications)
{
    auto result = s_registry.callTool("pixel_history", s_session,
        {{"x", 0}, {"y", 0}, {"eventId", s_firstDrawEid}});
    EXPECT_TRUE(result.contains("modifications"));
    EXPECT_TRUE(result["modifications"].is_array());
    EXPECT_TRUE(result.contains("targetId"));
    EXPECT_EQ(result["x"], 0);
}

TEST_F(Phase1ToolTest, PixelHistory_ModificationFields)
{
    auto result = s_registry.callTool("pixel_history", s_session,
        {{"x", 0}, {"y", 0}, {"eventId", s_firstDrawEid}});
    if (result["modifications"].size() > 0)
    {
        auto& mod = result["modifications"][0];
        EXPECT_TRUE(mod.contains("eventId"));
        EXPECT_TRUE(mod.contains("shaderOut"));
        EXPECT_TRUE(mod.contains("postMod"));
        EXPECT_TRUE(mod.contains("passed"));
        EXPECT_TRUE(mod.contains("flags"));
    }
}

// -- debug_pixel --------------------------------------------------------------

TEST_F(Phase1ToolTest, DebugPixel_SummaryMode)
{
    auto result = s_registry.callTool("debug_pixel", s_session,
        {{"eventId", s_firstDrawEid}, {"x", 50}, {"y", 50}});
    EXPECT_EQ(result["eventId"], s_firstDrawEid);
    EXPECT_TRUE(result.contains("stage"));
    EXPECT_TRUE(result.contains("totalSteps"));
    EXPECT_TRUE(result.contains("inputs"));
    EXPECT_TRUE(result.contains("outputs"));
    EXPECT_FALSE(result.contains("trace"));
}

TEST_F(Phase1ToolTest, DebugPixel_TraceMode)
{
    auto result = s_registry.callTool("debug_pixel", s_session,
        {{"eventId", s_firstDrawEid}, {"x", 50}, {"y", 50}, {"mode", "trace"}});
    EXPECT_TRUE(result.contains("trace"));
    if (result["trace"].size() > 0)
    {
        auto& step = result["trace"][0];
        EXPECT_TRUE(step.contains("step"));
        EXPECT_TRUE(step.contains("instruction"));
        EXPECT_TRUE(step.contains("changes"));
    }
}

TEST_F(Phase1ToolTest, DebugPixel_InputsHaveTypeInfo)
{
    auto result = s_registry.callTool("debug_pixel", s_session,
        {{"eventId", s_firstDrawEid}, {"x", 50}, {"y", 50}});
    if (result["inputs"].size() > 0)
    {
        auto& input = result["inputs"][0];
        EXPECT_TRUE(input.contains("name"));
        EXPECT_TRUE(input.contains("type"));
        EXPECT_TRUE(input.contains("rows"));
        EXPECT_TRUE(input.contains("cols"));
    }
}

// -- debug_vertex -------------------------------------------------------------

TEST_F(Phase1ToolTest, DebugVertex_SummaryMode)
{
    auto result = s_registry.callTool("debug_vertex", s_session,
        {{"eventId", s_firstDrawEid}, {"vertexId", 0}});
    EXPECT_EQ(result["eventId"], s_firstDrawEid);
    EXPECT_EQ(result["stage"], "vs");
    EXPECT_TRUE(result.contains("inputs"));
    EXPECT_TRUE(result.contains("outputs"));
}

// -- get_texture_stats --------------------------------------------------------

TEST_F(Phase1ToolTest, GetTextureStats_MinMax)
{
    if (s_textureResId.empty()) GTEST_SKIP() << "No texture resource found";

    auto result = s_registry.callTool("get_texture_stats", s_session,
        {{"resourceId", s_textureResId}});
    EXPECT_TRUE(result.contains("min"));
    EXPECT_TRUE(result.contains("max"));
    EXPECT_TRUE(result["min"].contains("floatValue"));
    EXPECT_TRUE(result["max"].contains("floatValue"));
    EXPECT_FALSE(result.contains("histogram"));
}

TEST_F(Phase1ToolTest, GetTextureStats_WithHistogram)
{
    if (s_textureResId.empty()) GTEST_SKIP() << "No texture resource found";

    auto result = s_registry.callTool("get_texture_stats", s_session,
        {{"resourceId", s_textureResId}, {"histogram", true}});
    EXPECT_TRUE(result.contains("histogram"));
    EXPECT_EQ(result["histogram"].size(), 256u);
    auto& bucket = result["histogram"][0];
    EXPECT_TRUE(bucket.contains("r"));
    EXPECT_TRUE(bucket.contains("g"));
    EXPECT_TRUE(bucket.contains("b"));
    EXPECT_TRUE(bucket.contains("a"));
}

TEST_F(Phase1ToolTest, GetTextureStats_InvalidResource_Throws)
{
    EXPECT_THROW(
        s_registry.callTool("get_texture_stats", s_session,
            {{"resourceId", "ResourceId::999999999"}}),
        std::exception);
}

TEST_F(Phase1ToolTest, GetTextureStats_InvalidMip_Throws)
{
    if (s_textureResId.empty()) GTEST_SKIP() << "No texture resource found";

    EXPECT_THROW(
        s_registry.callTool("get_texture_stats", s_session,
            {{"resourceId", s_textureResId}, {"mip", 99}}),
        std::exception);
}
