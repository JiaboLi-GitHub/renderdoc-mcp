#include <gtest/gtest.h>
#include "mcp/tool_registry.h"
#include "mcp/tools/tools.h"
#include "core/session.h"
#include "core/diff_session.h"
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;
using renderdoc::core::Session;
using renderdoc::core::DiffSession;
using renderdoc::mcp::ToolContext;
using renderdoc::mcp::ToolRegistry;
namespace tools = renderdoc::mcp::tools;

// Helper: try opening the capture in an SEH-guarded block so that headless
// machines without a GPU driver don't crash the entire test suite.
// The actual call is in openCaptureImpl which may throw SEH exceptions.
// doOpenCaptureSEH wraps it with __try/__except (no C++ objects allowed).
#ifdef _WIN32
static void openCaptureImpl(Session* s);

#pragma warning(push)
#pragma warning(disable: 4611)  // setjmp interaction
static bool doOpenCaptureSEH(Session* s)
{
    __try
    {
        openCaptureImpl(s);
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
#pragma warning(pop)

static void openCaptureImpl(Session* s)
{
    s->open(TEST_RDC_PATH);
}
#endif

class RenderdocToolTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        tools::registerSessionTools(s_registry);
        tools::registerEventTools(s_registry);
        tools::registerPipelineTools(s_registry);
        tools::registerExportTools(s_registry);
        tools::registerInfoTools(s_registry);
        tools::registerResourceTools(s_registry);
        tools::registerShaderTools(s_registry);

        // Open test capture
#ifdef _WIN32
        if(!doOpenCaptureSEH(&s_session))
        {
            s_skipAll = true;
            return;
        }
#else
        s_registry.callTool("open_capture", ctx(),
            {{"path", TEST_RDC_PATH}});
#endif
        ASSERT_TRUE(s_session.isOpen()) << "open_capture failed";

        // Navigate to first draw call
        auto draws = s_registry.callTool("list_draws", ctx(), {});
        ASSERT_TRUE(draws.contains("draws"));
        ASSERT_GT(draws["draws"].size(), 0u);
        uint32_t firstDrawEid = draws["draws"][0]["eventId"].get<uint32_t>();
        s_registry.callTool("goto_event", ctx(), {{"eventId", firstDrawEid}});
        s_firstDrawEventId = firstDrawEid;
    }

    static void TearDownTestSuite() {
        s_session.close();
    }

    void SetUp() override {
        if(s_skipAll)
            GTEST_SKIP() << "RenderDoc replay not available (no GPU or driver on this machine)";
    }

    static ToolContext& ctx() {
        static ToolContext c{s_session, s_diffSession};
        return c;
    }

    static Session s_session;
    static DiffSession s_diffSession;
    static ToolRegistry s_registry;
    static uint32_t s_firstDrawEventId;
    static bool s_skipAll;
};

Session RenderdocToolTest::s_session;
DiffSession RenderdocToolTest::s_diffSession;
ToolRegistry RenderdocToolTest::s_registry;
uint32_t RenderdocToolTest::s_firstDrawEventId = 0;
bool RenderdocToolTest::s_skipAll = false;

// -- open_capture -------------------------------------------------------------

TEST_F(RenderdocToolTest, OpenCapture_OpensCapture)
{
    EXPECT_TRUE(s_session.isOpen());
}

TEST_F(RenderdocToolTest, OpenCapture_InvalidPath_Throws)
{
    EXPECT_THROW(
        s_registry.callTool("open_capture", ctx(), {{"path", "/nonexistent.rdc"}}),
        std::exception);

    // Re-open the valid capture so other tests still work
    auto result = s_registry.callTool("open_capture", ctx(),
        {{"path", TEST_RDC_PATH}});
    ASSERT_FALSE(result.empty());
    // Re-navigate to the first draw
    auto draws = s_registry.callTool("list_draws", ctx(), {});
    if(draws.contains("draws") && draws["draws"].size() > 0)
    {
        uint32_t eid = draws["draws"][0]["eventId"].get<uint32_t>();
        s_registry.callTool("goto_event", ctx(), {{"eventId", eid}});
    }
}

// -- list_events --------------------------------------------------------------

TEST_F(RenderdocToolTest, ListEvents_ReturnsNonEmpty)
{
    auto result = s_registry.callTool("list_events", ctx(), {});
    ASSERT_TRUE(result.is_array());
    EXPECT_GT(result.size(), 0u);
}

TEST_F(RenderdocToolTest, ListEvents_InvalidFilter_ReturnsEmpty)
{
    auto result = s_registry.callTool("list_events", ctx(),
        {{"filter", "zzz_no_match_zzz"}});
    ASSERT_TRUE(result.is_array());
    EXPECT_EQ(result.size(), 0u);
}

// -- goto_event ---------------------------------------------------------------

TEST_F(RenderdocToolTest, GotoEvent_ValidId)
{
    EXPECT_NO_THROW(
        s_registry.callTool("goto_event", ctx(), {{"eventId", s_firstDrawEventId}}));
}

// goto_event with an invalid ID: RenderDoc's SetFrameEvent does not throw for
// out-of-range IDs (it silently clamps), so we just verify the call succeeds.
TEST_F(RenderdocToolTest, GotoEvent_InvalidId_NoThrow)
{
    EXPECT_NO_THROW(
        s_registry.callTool("goto_event", ctx(), {{"eventId", 999999}}));
    // Restore position
    s_registry.callTool("goto_event", ctx(), {{"eventId", s_firstDrawEventId}});
}

// -- list_draws ---------------------------------------------------------------

TEST_F(RenderdocToolTest, ListDraws_HasCorrectFields)
{
    auto result = s_registry.callTool("list_draws", ctx(), {});
    ASSERT_GT(result["draws"].size(), 0u);
    auto& draw = result["draws"][0];
    EXPECT_TRUE(draw.contains("eventId"));
    EXPECT_TRUE(draw.contains("name"));
    EXPECT_TRUE(draw.contains("flags"));
    EXPECT_TRUE(draw.contains("numIndices"));
    EXPECT_TRUE(draw.contains("numInstances"));
}

TEST_F(RenderdocToolTest, ListDraws_FilterNoMatch_ReturnsEmpty)
{
    auto result = s_registry.callTool("list_draws", ctx(),
        {{"filter", "zzz_no_match_zzz"}});
    EXPECT_EQ(result["draws"].size(), 0u);
}

// -- get_draw_info ------------------------------------------------------------

TEST_F(RenderdocToolTest, GetDrawInfo_ValidId)
{
    auto result = s_registry.callTool("get_draw_info", ctx(),
        {{"eventId", s_firstDrawEventId}});
    EXPECT_EQ(result["eventId"], s_firstDrawEventId);
    EXPECT_TRUE(result.contains("name"));
}

TEST_F(RenderdocToolTest, GetDrawInfo_InvalidId_Throws)
{
    EXPECT_THROW(
        s_registry.callTool("get_draw_info", ctx(), {{"eventId", 999999}}),
        std::exception);
}

// -- get_pipeline_state -------------------------------------------------------

TEST_F(RenderdocToolTest, GetPipelineState_ReturnsApiField)
{
    auto result = s_registry.callTool("get_pipeline_state", ctx(), {});
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.contains("api"));
}

TEST_F(RenderdocToolTest, GetPipelineState_WithEventId)
{
    auto result = s_registry.callTool("get_pipeline_state", ctx(),
        {{"eventId", s_firstDrawEventId}});
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.contains("api"));

    // Viewport depth range must be populated (vkcube uses standard 0..1 depth)
    if (result.contains("viewports") && result["viewports"].is_array()
        && !result["viewports"].empty()) {
        auto& vp0 = result["viewports"][0];
        EXPECT_DOUBLE_EQ(vp0.value("maxDepth", 0.0), 1.0)
            << "maxDepth should be 1.0, not the default 0.0";
        EXPECT_GE(vp0.value("width", 0.0), 1.0)
            << "viewport width should be positive";
    }

    // Render target info should have width/height populated
    if (result.contains("renderTargets") && result["renderTargets"].is_array()
        && !result["renderTargets"].empty()) {
        auto& rt0 = result["renderTargets"][0];
        EXPECT_GT(rt0.value("width", 0u), 0u)
            << "render target width should be populated";
        EXPECT_GT(rt0.value("height", 0u), 0u)
            << "render target height should be populated";
    }
}

// -- get_bindings -------------------------------------------------------------

TEST_F(RenderdocToolTest, GetBindings_ReturnsData)
{
    auto result = s_registry.callTool("get_bindings", ctx(), {});
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.contains("api"));
    EXPECT_TRUE(result.contains("stages"));
}

// -- get_shader ---------------------------------------------------------------

TEST_F(RenderdocToolTest, GetShader_ReturnsDisassembly)
{
    // Navigate to first draw to ensure a VS is bound
    s_registry.callTool("goto_event", ctx(), {{"eventId", s_firstDrawEventId}});
    auto result = s_registry.callTool("get_shader", ctx(),
        {{"stage", "vs"}, {"mode", "disasm"}});
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.contains("disassembly"));
    EXPECT_TRUE(result.contains("entryPoint"));
}

TEST_F(RenderdocToolTest, GetShader_UnboundStage_Throws)
{
    // Geometry shader likely not bound in vkcube - should throw
    s_registry.callTool("goto_event", ctx(), {{"eventId", s_firstDrawEventId}});
    EXPECT_THROW(
        s_registry.callTool("get_shader", ctx(), {{"stage", "gs"}, {"mode", "disasm"}}),
        std::exception);
}

// -- list_shaders -------------------------------------------------------------

TEST_F(RenderdocToolTest, ListShaders_ReturnsData)
{
    auto result = s_registry.callTool("list_shaders", ctx(), {});
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.contains("shaders"));
    EXPECT_TRUE(result.contains("count"));
}

// -- search_shaders -----------------------------------------------------------

TEST_F(RenderdocToolTest, SearchShaders_FindsMatch)
{
    auto result = s_registry.callTool("search_shaders", ctx(),
        {{"pattern", "main"}});
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.contains("matches"));
    EXPECT_TRUE(result.contains("count"));
}

TEST_F(RenderdocToolTest, SearchShaders_NoMatch_ReturnsEmptyMatches)
{
    auto result = s_registry.callTool("search_shaders", ctx(),
        {{"pattern", "zzz_absolutely_no_match_zzz"}});
    EXPECT_TRUE(result.contains("matches"));
    EXPECT_EQ(result["matches"].size(), 0u);
    EXPECT_EQ(result["count"].get<int>(), 0);
}

// -- get_capture_info ---------------------------------------------------------

TEST_F(RenderdocToolTest, GetCaptureInfo_ReturnsMetadata)
{
    auto result = s_registry.callTool("get_capture_info", ctx(), {});
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.contains("api"));
    EXPECT_TRUE(result.contains("totalEvents"));
    EXPECT_TRUE(result.contains("totalDraws"));
}

// -- get_stats ----------------------------------------------------------------

TEST_F(RenderdocToolTest, GetStats_ReturnsData)
{
    auto result = s_registry.callTool("get_stats", ctx(), {});
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.contains("perPass"));
    EXPECT_TRUE(result.contains("topDraws"));
    EXPECT_TRUE(result.contains("largestResources"));
}

// -- list_resources -----------------------------------------------------------

TEST_F(RenderdocToolTest, ListResources_ReturnsNonEmpty)
{
    auto result = s_registry.callTool("list_resources", ctx(), {});
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.contains("resources"));
    EXPECT_GT(result["resources"].size(), 0u);
}

// -- get_resource_info --------------------------------------------------------

TEST_F(RenderdocToolTest, GetResourceInfo_InvalidId_Throws)
{
    EXPECT_THROW(
        s_registry.callTool("get_resource_info", ctx(),
            {{"resourceId", "ResourceId::0"}}),
        std::exception);
}

// -- list_passes --------------------------------------------------------------

TEST_F(RenderdocToolTest, ListPasses_ReturnsList)
{
    auto result = s_registry.callTool("list_passes", ctx(), {});
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.contains("passes"));
    EXPECT_TRUE(result.contains("count"));
}

// -- get_pass_info ------------------------------------------------------------

TEST_F(RenderdocToolTest, GetPassInfo_ValidEventId)
{
    // Use the first draw event ID - get_pass_info finds by event ID in the
    // root action tree. It may not be a pass marker, but it will still find
    // the action (even though it may have no children). We test it doesn't throw.
    auto passes = s_registry.callTool("list_passes", ctx(), {});
    if(passes["passes"].size() > 0)
    {
        uint32_t passEid = passes["passes"][0]["eventId"].get<uint32_t>();
        auto result = s_registry.callTool("get_pass_info", ctx(),
            {{"eventId", passEid}});
        EXPECT_FALSE(result.empty());
        EXPECT_TRUE(result.contains("name"));
    }
    else
    {
        // vkcube might not have marker-delimited passes; just verify first draw works
        auto result = s_registry.callTool("get_pass_info", ctx(),
            {{"eventId", s_firstDrawEventId}});
        EXPECT_FALSE(result.empty());
    }
}

TEST_F(RenderdocToolTest, GetPassInfo_InvalidEventId_Throws)
{
    EXPECT_THROW(
        s_registry.callTool("get_pass_info", ctx(), {{"eventId", 999999}}),
        std::exception);
}

// -- export_render_target -----------------------------------------------------

TEST_F(RenderdocToolTest, ExportRenderTarget_CreatesPNG)
{
    s_registry.callTool("goto_event", ctx(), {{"eventId", s_firstDrawEventId}});
    auto result = s_registry.callTool("export_render_target", ctx(), {});
    if(result.contains("path"))
    {
        std::string path = result["path"].get<std::string>();
        EXPECT_TRUE(fs::exists(path));
        EXPECT_GT(fs::file_size(path), 0u);
    }
}

// -- export_texture -----------------------------------------------------------

TEST_F(RenderdocToolTest, ExportTexture_InvalidResourceId_Throws)
{
    EXPECT_THROW(
        s_registry.callTool("export_texture", ctx(),
            {{"resourceId", "ResourceId::0"}}),
        std::exception);
}

// -- export_buffer ------------------------------------------------------------

TEST_F(RenderdocToolTest, ExportBuffer_InvalidResourceId_WritesEmptyFile)
{
    // ResourceId::0 is not a valid buffer, but GetBufferData returns empty data
    // rather than throwing. The tool writes a 0-byte file.
    auto result = s_registry.callTool("export_buffer", ctx(),
        {{"resourceId", "ResourceId::0"}});
    EXPECT_TRUE(result.contains("path"));
    EXPECT_EQ(result["byteSize"].get<uint64_t>(), 0u);
}

// -- get_log ------------------------------------------------------------------

TEST_F(RenderdocToolTest, GetLog_ReturnsMessages)
{
    auto result = s_registry.callTool("get_log", ctx(), {});
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.contains("messages"));
    EXPECT_TRUE(result.contains("count"));
}
