#include "cli/cli_parse.h"
#include <gtest/gtest.h>

using namespace renderdoc::cli;
using renderdoc::core::ShaderStage;

// Helper: build argc/argv from initializer list
static Args parse(std::initializer_list<const char*> tokens) {
    std::vector<char*> argv;
    for (auto t : tokens)
        argv.push_back(const_cast<char*>(t));
    return parseArgs(static_cast<int>(argv.size()), argv.data());
}

// --- parseStage ---

TEST(ParseStage, ValidLowercase) {
    EXPECT_EQ(*parseStage("vs"), ShaderStage::Vertex);
    EXPECT_EQ(*parseStage("hs"), ShaderStage::Hull);
    EXPECT_EQ(*parseStage("ds"), ShaderStage::Domain);
    EXPECT_EQ(*parseStage("gs"), ShaderStage::Geometry);
    EXPECT_EQ(*parseStage("ps"), ShaderStage::Pixel);
    EXPECT_EQ(*parseStage("cs"), ShaderStage::Compute);
}

TEST(ParseStage, ValidUppercase) {
    EXPECT_EQ(*parseStage("VS"), ShaderStage::Vertex);
    EXPECT_EQ(*parseStage("PS"), ShaderStage::Pixel);
}

TEST(ParseStage, InvalidReturnsNullopt) {
    EXPECT_FALSE(parseStage("xx").has_value());
    EXPECT_FALSE(parseStage("").has_value());
    EXPECT_FALSE(parseStage("vertex").has_value());
}

// --- parseArgs basic commands ---

TEST(ParseArgs, InfoCommand) {
    auto a = parse({"cli", "test.rdc", "info"});
    EXPECT_EQ(a.capturePath, "test.rdc");
    EXPECT_EQ(a.command, "info");
}

TEST(ParseArgs, EventIdFlag) {
    auto a = parse({"cli", "test.rdc", "pipeline", "-e", "42"});
    EXPECT_EQ(a.command, "pipeline");
    ASSERT_TRUE(a.eventId.has_value());
    EXPECT_EQ(*a.eventId, 42u);
}

TEST(ParseArgs, EventIdLongFlag) {
    auto a = parse({"cli", "test.rdc", "pipeline", "--event", "99"});
    ASSERT_TRUE(a.eventId.has_value());
    EXPECT_EQ(*a.eventId, 99u);
}

TEST(ParseArgs, FilterFlag) {
    auto a = parse({"cli", "test.rdc", "events", "--filter", "Draw"});
    EXPECT_EQ(a.filter, "Draw");
}

TEST(ParseArgs, TypeFilter) {
    auto a = parse({"cli", "test.rdc", "resources", "--type", "Texture"});
    EXPECT_EQ(a.typeFilter, "Texture");
}

TEST(ParseArgs, OutputDir) {
    auto a = parse({"cli", "test.rdc", "export-rt", "0", "-o", "/tmp/out"});
    EXPECT_EQ(a.outputDir, "/tmp/out");
    ASSERT_EQ(a.positional.size(), 1u);
    EXPECT_EQ(a.positional[0], "0");
}

// --- capture command ---

TEST(ParseArgs, CaptureCommand) {
    auto a = parse({"cli", "capture", "MyApp.exe", "-d", "120", "-o", "out.rdc"});
    EXPECT_EQ(a.command, "capture");
    EXPECT_EQ(a.delayFrames, 120u);
    EXPECT_EQ(a.outputDir, "out.rdc");
    ASSERT_EQ(a.positional.size(), 1u);
    EXPECT_EQ(a.positional[0], "MyApp.exe");
}

TEST(ParseArgs, CaptureWorkingDir) {
    auto a = parse({"cli", "capture", "App.exe", "-w", "/home/user"});
    EXPECT_EQ(a.workingDir, "/home/user");
}

// --- expect flags ---

TEST(ParseArgs, ExpectRGBA) {
    auto a = parse({"cli", "test.rdc", "assert-pixel", "11", "100", "200",
                     "--expect", "1.0", "0.5", "0.0", "1.0"});
    EXPECT_TRUE(a.hasExpectRGBA);
    EXPECT_FLOAT_EQ(a.expectRGBA[0], 1.0f);
    EXPECT_FLOAT_EQ(a.expectRGBA[1], 0.5f);
    EXPECT_FLOAT_EQ(a.expectRGBA[2], 0.0f);
    EXPECT_FLOAT_EQ(a.expectRGBA[3], 1.0f);
}

TEST(ParseArgs, ExpectString) {
    auto a = parse({"cli", "test.rdc", "assert-state", "11", "path",
                     "--expect", "MyValue"});
    EXPECT_FALSE(a.hasExpectRGBA);
    EXPECT_EQ(a.expectStr, "MyValue");
}

TEST(ParseArgs, ExpectInt) {
    auto a = parse({"cli", "test.rdc", "assert-count", "draws",
                     "--expect", "5", "--op", "eq"});
    EXPECT_EQ(a.expectCount, 5);
    EXPECT_EQ(a.opStr, "eq");
}

// --- numeric flags ---

TEST(ParseArgs, ToleranceFlag) {
    auto a = parse({"cli", "test.rdc", "assert-pixel", "11", "0", "0",
                     "--expect", "1.0", "0.0", "0.0", "1.0",
                     "--tolerance", "0.05"});
    EXPECT_FLOAT_EQ(a.tolerance, 0.05f);
}

TEST(ParseArgs, ThresholdFlag) {
    auto a = parse({"cli", "test.rdc", "assert-image", "a.png", "b.png",
                     "--threshold", "1.5"});
    EXPECT_DOUBLE_EQ(a.threshold, 1.5);
}

TEST(ParseArgs, TargetFlag) {
    auto a = parse({"cli", "test.rdc", "pixel", "10", "20", "--target", "2"});
    EXPECT_EQ(a.targetIndex, 2u);
}

// --- multiple flags combined ---

TEST(ParseArgs, MultipleFlags) {
    auto a = parse({"cli", "test.rdc", "debug", "pixel", "10", "20",
                     "-e", "42", "--trace", "--primitive", "7"});
    EXPECT_EQ(a.command, "debug");
    ASSERT_TRUE(a.eventId.has_value());
    EXPECT_EQ(*a.eventId, 42u);
    EXPECT_TRUE(a.trace);
    EXPECT_EQ(a.primitive, 7u);
}

// --- invalid numeric exits (death tests) ---

TEST(ParseArgsDeathTest, InvalidEventId) {
    EXPECT_EXIT(
        parse({"cli", "test.rdc", "pipeline", "-e", "abc"}),
        ::testing::ExitedWithCode(1),
        "invalid value.*-e/--event"
    );
}

TEST(ParseArgsDeathTest, InvalidTarget) {
    EXPECT_EXIT(
        parse({"cli", "test.rdc", "pixel", "0", "0", "--target", "xyz"}),
        ::testing::ExitedWithCode(1),
        "invalid value.*--target"
    );
}

TEST(ParseArgsDeathTest, TooFewArgs) {
    EXPECT_EXIT(
        parse({"cli"}),
        ::testing::ExitedWithCode(1),
        ""
    );
}

// --- shader/encoding flags ---

TEST(ParseArgs, ShaderFlags) {
    auto a = parse({"cli", "test.rdc", "shader-build", "test.hlsl",
                     "--stage", "ps", "--encoding", "HLSL", "--entry", "PSMain"});
    EXPECT_EQ(a.stageStr, "ps");
    EXPECT_EQ(a.encoding, "HLSL");
    EXPECT_EQ(a.entry, "PSMain");
}

TEST(ParseArgs, WithShaderId) {
    auto a = parse({"cli", "test.rdc", "shader-replace", "42", "ps",
                     "--with", "999"});
    EXPECT_EQ(a.withShaderId, 999u);
}

// --- defaults ---

TEST(ParseArgs, DefaultValues) {
    auto a = parse({"cli", "test.rdc", "info"});
    EXPECT_FALSE(a.eventId.has_value());
    EXPECT_EQ(a.delayFrames, 100u);
    EXPECT_EQ(a.targetIndex, 0u);
    EXPECT_FLOAT_EQ(a.tolerance, 0.01f);
    EXPECT_EQ(a.opStr, "eq");
    EXPECT_EQ(a.entry, "main");
    EXPECT_EQ(a.format, "obj");
    EXPECT_EQ(a.minSeverity, "high");
    EXPECT_DOUBLE_EQ(a.threshold, 0.0);
}
