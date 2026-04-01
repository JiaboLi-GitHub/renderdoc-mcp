#include <gtest/gtest.h>
#include "mcp/tool_registry.h"
#include "mcp/tools/tools.h"
#include "core/session.h"

#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;
using renderdoc::core::Session;
using renderdoc::mcp::ToolRegistry;
namespace tools = renderdoc::mcp::tools;
namespace fs = std::filesystem;

#ifdef _WIN32
static void openCaptureImpl3(Session* s);

#pragma warning(push)
#pragma warning(disable: 4611)
static bool doOpenCaptureSEH3(Session* s)
{
    __try { openCaptureImpl3(s); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#pragma warning(pop)

static void openCaptureImpl3(Session* s) { s->open(TEST_RDC_PATH); }
#endif

class Phase2ToolTest : public ::testing::Test {
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
        tools::registerShaderEditTools(s_registry);
        tools::registerMeshTools(s_registry);
        tools::registerSnapshotTools(s_registry);
        tools::registerUsageTools(s_registry);
        tools::registerAssertionTools(s_registry);

#ifdef _WIN32
        if (!doOpenCaptureSEH3(&s_session)) { s_skipAll = true; return; }
#else
        s_session.open(TEST_RDC_PATH);
#endif
        ASSERT_TRUE(s_session.isOpen());

        auto draws = s_registry.callTool("list_draws", s_session, {});
        ASSERT_TRUE(draws.contains("draws"));
        ASSERT_GT(draws["draws"].size(), 0u);
        s_firstDrawEid = draws["draws"][0]["eventId"].get<uint32_t>();
        s_registry.callTool("goto_event", s_session, {{"eventId", s_firstDrawEid}});

        auto resources = s_registry.callTool("list_resources", s_session, {{"type", "Texture"}});
        if (resources.contains("resources") && resources["resources"].size() > 0)
            s_textureResId = resources["resources"][0]["resourceId"].get<std::string>();
    }

    static void TearDownTestSuite() { s_session.close(); }

    void SetUp() override {
        if (s_skipAll)
            GTEST_SKIP() << "RenderDoc replay not available";
    }

    static Session s_session;
    static ToolRegistry s_registry;
    static uint32_t s_firstDrawEid;
    static std::string s_textureResId;
    static bool s_skipAll;
};

Session Phase2ToolTest::s_session;
ToolRegistry Phase2ToolTest::s_registry;
uint32_t Phase2ToolTest::s_firstDrawEid = 0;
std::string Phase2ToolTest::s_textureResId;
bool Phase2ToolTest::s_skipAll = false;

// -- shader_encodings ---------------------------------------------------------

TEST_F(Phase2ToolTest, ShaderEncodings_ReturnsList) {
    auto result = s_registry.callTool("shader_encodings", s_session, {});
    EXPECT_TRUE(result.contains("encodings"));
    EXPECT_TRUE(result["encodings"].is_array());
    EXPECT_GT(result["encodings"].size(), 0u);
}

// -- export_mesh --------------------------------------------------------------

TEST_F(Phase2ToolTest, ExportMesh_ReturnsOBJ) {
    auto result = s_registry.callTool("export_mesh", s_session,
        {{"eventId", s_firstDrawEid}, {"format", "obj"}});
    EXPECT_TRUE(result.contains("obj"));
    EXPECT_TRUE(result.contains("vertexCount"));
    EXPECT_GT(result["vertexCount"].get<int>(), 0);
}

TEST_F(Phase2ToolTest, ExportMesh_ReturnsJSON) {
    auto result = s_registry.callTool("export_mesh", s_session,
        {{"eventId", s_firstDrawEid}, {"format", "json"}});
    EXPECT_TRUE(result.contains("vertices"));
    EXPECT_TRUE(result.contains("faces"));
    EXPECT_TRUE(result.contains("topology"));
}

// -- export_snapshot ----------------------------------------------------------

TEST_F(Phase2ToolTest, ExportSnapshot_CreatesFiles) {
    std::string tmpDir = (fs::temp_directory_path() / "rdc_snapshot_test").string();
    fs::remove_all(tmpDir);

    auto result = s_registry.callTool("export_snapshot", s_session,
        {{"eventId", s_firstDrawEid}, {"outputDir", tmpDir}});

    EXPECT_TRUE(result.contains("manifestPath"));
    EXPECT_TRUE(result.contains("files"));
    EXPECT_GT(result["files"].size(), 0u);
    EXPECT_TRUE(fs::exists(fs::path(tmpDir) / "manifest.json"));
    EXPECT_TRUE(fs::exists(fs::path(tmpDir) / "pipeline.json"));

    fs::remove_all(tmpDir);
}

// -- get_resource_usage -------------------------------------------------------

TEST_F(Phase2ToolTest, ResourceUsage_ReturnsList) {
    if (s_textureResId.empty()) GTEST_SKIP() << "No texture resource found";

    auto result = s_registry.callTool("get_resource_usage", s_session,
        {{"resourceId", s_textureResId}});
    EXPECT_TRUE(result.contains("entries"));
    EXPECT_TRUE(result["entries"].is_array());
}

// -- assert_pixel -------------------------------------------------------------

TEST_F(Phase2ToolTest, AssertPixel_MatchesPickPixel) {
    auto pick = s_registry.callTool("pick_pixel", s_session,
        {{"x", 0}, {"y", 0}, {"eventId", s_firstDrawEid}});

    float r = pick["color"]["floatValue"][0].get<float>();
    float g = pick["color"]["floatValue"][1].get<float>();
    float b = pick["color"]["floatValue"][2].get<float>();
    float a = pick["color"]["floatValue"][3].get<float>();

    auto result = s_registry.callTool("assert_pixel", s_session,
        {{"eventId", s_firstDrawEid}, {"x", 0}, {"y", 0},
         {"expected", {r, g, b, a}}, {"tolerance", 0.01}});

    EXPECT_TRUE(result["pass"].get<bool>());
}

TEST_F(Phase2ToolTest, AssertPixel_FailsOnWrongColor) {
    auto result = s_registry.callTool("assert_pixel", s_session,
        {{"eventId", s_firstDrawEid}, {"x", 0}, {"y", 0},
         {"expected", {-999.0, -999.0, -999.0, -999.0}}, {"tolerance", 0.001}});

    EXPECT_FALSE(result["pass"].get<bool>());
}

// -- assert_state -------------------------------------------------------------

TEST_F(Phase2ToolTest, AssertState_ChecksApi) {
    auto result = s_registry.callTool("assert_state", s_session,
        {{"eventId", s_firstDrawEid}, {"path", "api"}, {"expected", "Vulkan"}});

    EXPECT_TRUE(result["pass"].get<bool>());
}

TEST_F(Phase2ToolTest, AssertState_FailsOnWrongValue) {
    auto result = s_registry.callTool("assert_state", s_session,
        {{"eventId", s_firstDrawEid}, {"path", "api"}, {"expected", "D3D12"}});

    EXPECT_FALSE(result["pass"].get<bool>());
}

// -- assert_count -------------------------------------------------------------

TEST_F(Phase2ToolTest, AssertCount_DrawsGtZero) {
    auto result = s_registry.callTool("assert_count", s_session,
        {{"what", "draws"}, {"expected", 0}, {"op", "gt"}});

    EXPECT_TRUE(result["pass"].get<bool>());
}

TEST_F(Phase2ToolTest, AssertCount_EventsExact) {
    auto status = s_session.status();
    int totalEvents = static_cast<int>(status.totalEvents);

    auto result = s_registry.callTool("assert_count", s_session,
        {{"what", "events"}, {"expected", totalEvents}, {"op", "eq"}});

    EXPECT_TRUE(result["pass"].get<bool>());
}

// -- assert_clean -------------------------------------------------------------

TEST_F(Phase2ToolTest, AssertClean_ReturnsResult) {
    auto result = s_registry.callTool("assert_clean", s_session, {});
    EXPECT_TRUE(result.contains("pass"));
    EXPECT_TRUE(result.contains("count"));
    EXPECT_TRUE(result.contains("minSeverity"));
}
