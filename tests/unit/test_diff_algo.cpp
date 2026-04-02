#include "core/diff.h"
#include <gtest/gtest.h>

using namespace renderdoc::core;

// ---------------------------------------------------------------------------
// lcsAlign tests
// ---------------------------------------------------------------------------

TEST(LcsAlign, IdenticalSequences)
{
    std::vector<std::string> keys = {"x", "y", "z"};
    auto pairs = lcsAlign(keys, keys);
    ASSERT_EQ(pairs.size(), 3u);
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_TRUE(pairs[i].first.has_value());
        EXPECT_TRUE(pairs[i].second.has_value());
        EXPECT_EQ(*pairs[i].first, i);
        EXPECT_EQ(*pairs[i].second, i);
    }
}

TEST(LcsAlign, CompletelyDifferent)
{
    std::vector<std::string> a = {"a", "b"};
    std::vector<std::string> b = {"c", "d"};
    auto pairs = lcsAlign(a, b);
    // 2 deleted + 2 added = 4 pairs total
    ASSERT_EQ(pairs.size(), 4u);

    int deleted = 0, added = 0;
    for (auto& p : pairs) {
        if (p.first.has_value() && !p.second.has_value()) ++deleted;
        if (!p.first.has_value() && p.second.has_value()) ++added;
    }
    EXPECT_EQ(deleted, 2);
    EXPECT_EQ(added, 2);
}

TEST(LcsAlign, EmptyA)
{
    std::vector<std::string> a;
    std::vector<std::string> b = {"x", "y"};
    auto pairs = lcsAlign(a, b);
    ASSERT_EQ(pairs.size(), 2u);
    for (auto& p : pairs) {
        EXPECT_FALSE(p.first.has_value());
        EXPECT_TRUE(p.second.has_value());
    }
}

TEST(LcsAlign, EmptyB)
{
    std::vector<std::string> a = {"x", "y"};
    std::vector<std::string> b;
    auto pairs = lcsAlign(a, b);
    ASSERT_EQ(pairs.size(), 2u);
    for (auto& p : pairs) {
        EXPECT_TRUE(p.first.has_value());
        EXPECT_FALSE(p.second.has_value());
    }
}

TEST(LcsAlign, BothEmpty)
{
    auto pairs = lcsAlign({}, {});
    EXPECT_TRUE(pairs.empty());
}

TEST(LcsAlign, InsertionInMiddle)
{
    std::vector<std::string> a = {"a", "b", "c"};
    std::vector<std::string> b = {"a", "X", "b", "c"};
    auto pairs = lcsAlign(a, b);
    // Expected: a-a matched, X added, b-b matched, c-c matched
    ASSERT_EQ(pairs.size(), 4u);

    // Count matched and added
    int matched = 0, added = 0;
    for (auto& p : pairs) {
        if (p.first.has_value() && p.second.has_value()) ++matched;
        if (!p.first.has_value() && p.second.has_value()) ++added;
    }
    EXPECT_EQ(matched, 3);
    EXPECT_EQ(added, 1);
}

TEST(LcsAlign, DeletionInMiddle)
{
    std::vector<std::string> a = {"a", "X", "b", "c"};
    std::vector<std::string> b = {"a", "b", "c"};
    auto pairs = lcsAlign(a, b);
    // Expected: a-a matched, X deleted, b-b matched, c-c matched
    ASSERT_EQ(pairs.size(), 4u);

    int matched = 0, deleted = 0;
    for (auto& p : pairs) {
        if (p.first.has_value() && p.second.has_value()) ++matched;
        if (p.first.has_value() && !p.second.has_value()) ++deleted;
    }
    EXPECT_EQ(matched, 3);
    EXPECT_EQ(deleted, 1);
}

TEST(LcsAlign, DuplicateKeys)
{
    std::vector<std::string> a = {"D", "D", "E"};
    std::vector<std::string> b = {"D", "D", "D", "E"};
    auto pairs = lcsAlign(a, b);
    // LCS = {"D","D","E"} (length 3), so 3 matched + 1 added = 4 pairs
    ASSERT_EQ(pairs.size(), 4u);

    int matched = 0, added = 0;
    for (auto& p : pairs) {
        if (p.first.has_value() && p.second.has_value()) ++matched;
        if (!p.first.has_value() && p.second.has_value()) ++added;
    }
    EXPECT_EQ(matched, 3);
    EXPECT_EQ(added, 1);
}

// ---------------------------------------------------------------------------
// makeDrawMatchKey tests
// ---------------------------------------------------------------------------

TEST(MatchKey, MarkerMode)
{
    DrawRecord rec;
    rec.markerPath  = "GBuffer/Floor";
    rec.drawType    = "DrawIndexed";
    rec.shaderHash  = "unused";
    rec.topology    = "TriangleList";

    std::string key = makeDrawMatchKey(rec, /*hasMarkers=*/true);
    EXPECT_EQ(key, "GBuffer/Floor|DrawIndexed");
}

TEST(MatchKey, FallbackMode)
{
    DrawRecord rec;
    rec.markerPath  = "irrelevant";
    rec.drawType    = "DrawIndexed";
    rec.shaderHash  = "abc123";
    rec.topology    = "TriangleList";

    std::string key = makeDrawMatchKey(rec, /*hasMarkers=*/false);
    EXPECT_EQ(key, "DrawIndexed|abc123|TriangleList");
}

TEST(MatchKey, MarkerModeIgnoresShaderHash)
{
    DrawRecord rec;
    rec.markerPath  = "Pass/Obj";
    rec.drawType    = "Draw";
    rec.shaderHash  = "secret_hash_xyz";
    rec.topology    = "TriangleList";

    std::string key = makeDrawMatchKey(rec, /*hasMarkers=*/true);
    EXPECT_EQ(key.find("secret_hash_xyz"), std::string::npos)
        << "shaderHash should not appear in key when hasMarkers=true";
}
