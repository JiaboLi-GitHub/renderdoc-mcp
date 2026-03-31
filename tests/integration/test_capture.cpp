#include <gtest/gtest.h>
#include "core/capture.h"
#include "core/errors.h"
#include "core/info.h"
#include "core/session.h"

#include <filesystem>

using namespace renderdoc::core;

// This test requires a real graphics application and RenderDoc.
// It is NOT run in CI. Run manually with:
//   test-capture --gtest_filter="CaptureFrame.*"
//
// Set environment variable RENDERDOC_TEST_EXE to the path of a graphics app.
// Example: set RENDERDOC_TEST_EXE=C:\Windows\System32\mspaint.exe

class CaptureFrameTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* exe = std::getenv("RENDERDOC_TEST_EXE");
        if (!exe || !std::filesystem::exists(exe)) {
            GTEST_SKIP() << "Set RENDERDOC_TEST_EXE to a graphics app for capture tests";
        }
        testExe = exe;
    }

    std::string testExe;
    Session session;
};

TEST_F(CaptureFrameTest, CaptureAndOpen) {
    CaptureRequest req;
    req.exePath = testExe;
    req.delayFrames = 5; // low delay for test speed

    CaptureResult result = captureFrame(session, req);

    EXPECT_FALSE(result.capturePath.empty());
    EXPECT_TRUE(std::filesystem::exists(result.capturePath));
    EXPECT_GT(result.pid, 0u);

    // Session should be open with valid capture
    EXPECT_TRUE(session.isOpen());

    CaptureInfo info = getCaptureInfo(session);
    EXPECT_GT(info.totalEvents, 0u);

    // Cleanup
    session.close();
    std::filesystem::remove(result.capturePath);
}

TEST_F(CaptureFrameTest, InvalidExePath) {
    CaptureRequest req;
    req.exePath = "C:/nonexistent/app.exe";

    EXPECT_THROW(captureFrame(session, req), CoreError);
}
