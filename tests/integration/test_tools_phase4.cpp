#include <gtest/gtest.h>
#include "mcp/tool_registry.h"
#include "mcp/tools/tools.h"
#include "core/session.h"
#include "core/diff_session.h"

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;
using renderdoc::core::Session;
using renderdoc::core::DiffSession;
using renderdoc::mcp::ToolContext;
using renderdoc::mcp::ToolRegistry;
namespace tools = renderdoc::mcp::tools;

#ifdef _WIN32
static void openCaptureImpl4(Session* s);

#pragma warning(push)
#pragma warning(disable: 4611)
static bool doOpenCaptureSEH4(Session* s)
{
    __try { openCaptureImpl4(s); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#pragma warning(pop)

static void openCaptureImpl4(Session* s) { s->open(TEST_RDC_PATH); }
#endif

class Phase4ToolTest : public ::testing::Test {
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
        tools::registerDiffTools(s_registry);
        tools::registerPassTools(s_registry);

#ifdef _WIN32
        if (!doOpenCaptureSEH4(&s_session)) { s_skipAll = true; return; }
#else
        s_session.open(TEST_RDC_PATH);
#endif
        ASSERT_TRUE(s_session.isOpen());

        auto draws = s_registry.callTool("list_draws", ToolContext{s_session, s_diffSession}, {});
        ASSERT_TRUE(draws.contains("draws"));
        ASSERT_GT(draws["draws"].size(), 0u);
        s_firstDrawEid = draws["draws"][0]["eventId"].get<uint32_t>();
    }

    static void TearDownTestSuite() { s_session.close(); }

    void SetUp() override {
        if (s_skipAll)
            GTEST_SKIP() << "RenderDoc replay not available";
    }

    static Session s_session;
    static DiffSession s_diffSession;
    static ToolRegistry s_registry;
    static uint32_t s_firstDrawEid;
    static bool s_skipAll;
};

Session Phase4ToolTest::s_session;
DiffSession Phase4ToolTest::s_diffSession;
ToolRegistry Phase4ToolTest::s_registry;
uint32_t Phase4ToolTest::s_firstDrawEid = 0;
bool Phase4ToolTest::s_skipAll = false;

TEST_F(Phase4ToolTest, PassAttachments_ReturnsValidStructure) {
    auto result = s_registry.callTool("get_pass_attachments",
        ToolContext{s_session, s_diffSession}, {{"eventId", s_firstDrawEid}});

    EXPECT_TRUE(result.contains("passName"));
    EXPECT_TRUE(result.contains("eventId"));
    EXPECT_TRUE(result.contains("colorTargets"));
    EXPECT_TRUE(result["colorTargets"].is_array());
    EXPECT_TRUE(result.contains("hasDepth"));
    EXPECT_TRUE(result.contains("synthetic"));

    if (result["colorTargets"].size() > 0) {
        auto& ct = result["colorTargets"][0];
        EXPECT_TRUE(ct.contains("resourceId"));
        EXPECT_TRUE(ct["resourceId"].get<std::string>().rfind("ResourceId::", 0) == 0);
        EXPECT_TRUE(ct.contains("width"));
        EXPECT_GT(ct["width"].get<uint32_t>(), 0u);
        EXPECT_TRUE(ct.contains("height"));
        EXPECT_GT(ct["height"].get<uint32_t>(), 0u);
    }
}

TEST_F(Phase4ToolTest, PassStatistics_ReturnsNonEmpty) {
    auto result = s_registry.callTool("get_pass_statistics",
        ToolContext{s_session, s_diffSession}, {});

    EXPECT_TRUE(result.contains("passes"));
    EXPECT_TRUE(result.contains("count"));
    EXPECT_GE(result["count"].get<uint32_t>(), 1u);

    auto& passes = result["passes"];
    EXPECT_TRUE(passes.is_array());
    EXPECT_GE(passes.size(), 1u);

    auto& p = passes[0];
    EXPECT_TRUE(p.contains("name"));
    EXPECT_TRUE(p.contains("eventId"));
    EXPECT_TRUE(p.contains("drawCount"));
    EXPECT_TRUE(p.contains("dispatchCount"));
    EXPECT_TRUE(p.contains("totalTriangles"));
    EXPECT_TRUE(p.contains("rtWidth"));
    EXPECT_TRUE(p.contains("attachmentCount"));
    EXPECT_TRUE(p.contains("synthetic"));

    uint32_t draws = p["drawCount"].get<uint32_t>();
    uint32_t dispatches = p["dispatchCount"].get<uint32_t>();
    EXPECT_GT(draws + dispatches, 0u);
}

TEST_F(Phase4ToolTest, PassStatistics_DrawCountMatchesListDraws) {
    auto statsResult = s_registry.callTool("get_pass_statistics",
        ToolContext{s_session, s_diffSession}, {});
    auto drawsResult = s_registry.callTool("list_draws",
        ToolContext{s_session, s_diffSession}, {});

    uint32_t totalFromStats = 0;
    for (const auto& p : statsResult["passes"])
        totalFromStats += p["drawCount"].get<uint32_t>();

    uint32_t totalFromDraws = static_cast<uint32_t>(drawsResult["draws"].size());

    EXPECT_GE(totalFromStats, 1u);
    EXPECT_GE(totalFromDraws, 1u);
}

TEST_F(Phase4ToolTest, PassDeps_ReturnsValidStructure) {
    auto result = s_registry.callTool("get_pass_deps",
        ToolContext{s_session, s_diffSession}, {});

    EXPECT_TRUE(result.contains("edges"));
    EXPECT_TRUE(result["edges"].is_array());
    EXPECT_TRUE(result.contains("passCount"));
    EXPECT_TRUE(result.contains("edgeCount"));
    EXPECT_GE(result["passCount"].get<uint32_t>(), 1u);

    EXPECT_EQ(result["edgeCount"].get<uint32_t>(),
              static_cast<uint32_t>(result["edges"].size()));

    if (result["edges"].size() > 0) {
        auto& e = result["edges"][0];
        EXPECT_TRUE(e.contains("srcPass"));
        EXPECT_TRUE(e.contains("dstPass"));
        EXPECT_TRUE(e.contains("resources"));
        EXPECT_TRUE(e["resources"].is_array());
        if (e["resources"].size() > 0) {
            EXPECT_TRUE(e["resources"][0].get<std::string>().rfind("ResourceId::", 0) == 0);
        }
    }
}

TEST_F(Phase4ToolTest, FindUnusedTargets_ReturnsValidStructure) {
    auto result = s_registry.callTool("find_unused_targets",
        ToolContext{s_session, s_diffSession}, {});

    EXPECT_TRUE(result.contains("unused"));
    EXPECT_TRUE(result["unused"].is_array());
    EXPECT_TRUE(result.contains("unusedCount"));
    EXPECT_TRUE(result.contains("totalTargets"));

    EXPECT_EQ(result["unusedCount"].get<uint32_t>(),
              static_cast<uint32_t>(result["unused"].size()));

    if (result["unused"].size() > 0) {
        auto& u = result["unused"][0];
        EXPECT_TRUE(u.contains("resourceId"));
        EXPECT_TRUE(u["resourceId"].get<std::string>().rfind("ResourceId::", 0) == 0);
        EXPECT_TRUE(u.contains("name"));
        EXPECT_TRUE(u.contains("writtenBy"));
        EXPECT_TRUE(u.contains("wave"));
    }
}
