#include <gtest/gtest.h>
#include "mcp/tool_registry.h"
#include "mcp/tools/tools.h"
#include "core/session.h"
#include "core/diff_session.h"
#include "core/errors.h"

#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;
using renderdoc::core::Session;
using renderdoc::core::DiffSession;
using renderdoc::core::CoreError;
using renderdoc::mcp::ToolContext;
using renderdoc::mcp::ToolRegistry;
namespace tools = renderdoc::mcp::tools;
namespace fs = std::filesystem;

#ifdef _WIN32
static void openDiffSessionImpl(DiffSession* ds);

#pragma warning(push)
#pragma warning(disable: 4611)
static bool doOpenDiffSessionSEH(DiffSession* ds)
{
    __try { openDiffSessionImpl(ds); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#pragma warning(pop)

static void openDiffSessionImpl(DiffSession* ds) { ds->open(TEST_RDC_PATH, TEST_RDC_PATH); }
#endif

class DiffToolTest : public ::testing::Test {
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

#ifdef _WIN32
        if (!doOpenDiffSessionSEH(&s_diffSession)) { s_skipAll = true; return; }
#else
        s_diffSession.open(TEST_RDC_PATH, TEST_RDC_PATH);
#endif
        if (!s_diffSession.isOpen()) { s_skipAll = true; return; }
    }

    static void TearDownTestSuite() { s_diffSession.close(); }

    void SetUp() override {
        if (s_skipAll)
            GTEST_SKIP() << "RenderDoc replay not available";
    }

    static ToolContext& ctx() {
        static ToolContext c{s_session, s_diffSession};
        return c;
    }

    static Session s_session;
    static DiffSession s_diffSession;
    static ToolRegistry s_registry;
    static bool s_skipAll;
};

Session DiffToolTest::s_session;
DiffSession DiffToolTest::s_diffSession;
ToolRegistry DiffToolTest::s_registry;
bool DiffToolTest::s_skipAll = false;

// -- diff_summary (self-diff) -------------------------------------------------

TEST_F(DiffToolTest, SelfDiffSummaryIsIdentical) {
    auto result = s_registry.callTool("diff_summary", ctx(), {});
    ASSERT_TRUE(result.contains("identical"));
    EXPECT_TRUE(result["identical"].get<bool>());
    ASSERT_TRUE(result.contains("divergedAt"));
    EXPECT_EQ(result["divergedAt"].get<std::string>(), "");
}

// -- diff_draws (self-diff) ---------------------------------------------------

TEST_F(DiffToolTest, SelfDiffDrawsAllEqual) {
    auto result = s_registry.callTool("diff_draws", ctx(), {});
    ASSERT_TRUE(result.contains("modified"));
    ASSERT_TRUE(result.contains("added"));
    ASSERT_TRUE(result.contains("deleted"));
    ASSERT_TRUE(result.contains("unchanged"));
    EXPECT_EQ(result["modified"].get<int>(), 0);
    EXPECT_EQ(result["added"].get<int>(), 0);
    EXPECT_EQ(result["deleted"].get<int>(), 0);
    EXPECT_GT(result["unchanged"].get<int>(), 0);
}

// -- diff_resources (self-diff) -----------------------------------------------

TEST_F(DiffToolTest, SelfDiffResourcesAllEqual) {
    auto result = s_registry.callTool("diff_resources", ctx(), {});
    ASSERT_TRUE(result.contains("modified"));
    ASSERT_TRUE(result.contains("added"));
    ASSERT_TRUE(result.contains("deleted"));
    EXPECT_EQ(result["modified"].get<int>(), 0);
    EXPECT_EQ(result["added"].get<int>(), 0);
    EXPECT_EQ(result["deleted"].get<int>(), 0);
}

// -- diff_framebuffer (self-diff) ---------------------------------------------

TEST_F(DiffToolTest, SelfDiffFramebufferIdentical) {
    auto result = s_registry.callTool("diff_framebuffer", ctx(), {});
    ASSERT_TRUE(result.contains("diffPixels"));
    EXPECT_EQ(result["diffPixels"].get<int>(), 0);
}

// -- diff_close ---------------------------------------------------------------

TEST_F(DiffToolTest, DiffCloseSucceeds) {
    // Open a fresh temporary diff session then close it via the tool
    DiffSession tmpSession;

#ifdef _WIN32
    if (!doOpenDiffSessionSEH(&tmpSession))
        GTEST_SKIP() << "Could not open tmp diff session";
#else
    tmpSession.open(TEST_RDC_PATH, TEST_RDC_PATH);
#endif
    ASSERT_TRUE(tmpSession.isOpen());

    ToolContext tmpCtx{s_session, tmpSession};
    auto result = s_registry.callTool("diff_close", tmpCtx, {});
    EXPECT_TRUE(result.contains("success"));
    EXPECT_TRUE(result["success"].get<bool>());
    EXPECT_FALSE(tmpSession.isOpen());
}

// -- diff_summary with no open diff session -----------------------------------

TEST_F(DiffToolTest, DiffNotOpenError) {
    DiffSession emptySession;
    ASSERT_FALSE(emptySession.isOpen());

    ToolContext emptyCtx{s_session, emptySession};
    EXPECT_THROW(
        s_registry.callTool("diff_summary", emptyCtx, {}),
        CoreError
    );
}
