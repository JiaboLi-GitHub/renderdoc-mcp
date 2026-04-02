#include <gtest/gtest.h>
#include "core/types.h"
#include <map>
#include <set>

using namespace renderdoc::core;

TEST(PassAnalysisUnit, PassRangeDefaultValues) {
    PassRange pr;
    EXPECT_EQ(pr.name, "");
    EXPECT_EQ(pr.beginEventId, 0u);
    EXPECT_EQ(pr.endEventId, 0u);
    EXPECT_EQ(pr.firstDrawEventId, 0u);
    EXPECT_FALSE(pr.synthetic);
}

TEST(PassAnalysisUnit, AttachmentInfoDefaultValues) {
    AttachmentInfo ai;
    EXPECT_EQ(ai.resourceId, 0u);
    EXPECT_EQ(ai.width, 0u);
    EXPECT_EQ(ai.height, 0u);
    EXPECT_EQ(ai.format, "");
}

// ---- Dependency DAG algorithm tests ----

struct MockUsage {
    uint64_t resourceId;
    uint32_t eventId;
    std::string usage;
};

static bool isWriteUsage(const std::string& usage) {
    return usage == "ColorTarget" || usage == "DepthStencilTarget" ||
           usage == "CopyDst" || usage == "Clear" || usage == "GenMips" ||
           usage == "ResolveDst";
}

static bool isReadUsage(const std::string& usage) {
    return usage.find("_Resource") != std::string::npos ||
           usage.find("_Constants") != std::string::npos ||
           usage == "VertexBuffer" || usage == "IndexBuffer" ||
           usage == "CopySrc" || usage == "Indirect" ||
           usage == "InputTarget" || usage == "ResolveSrc";
}

struct TestEdge {
    std::string src, dst;
    std::vector<uint64_t> resources;
};

static std::vector<TestEdge> buildEdges(
    const std::vector<PassRange>& passes,
    const std::vector<MockUsage>& usages)
{
    auto findPass = [&](uint32_t eid) -> int {
        for (size_t i = 0; i < passes.size(); i++) {
            if (eid >= passes[i].beginEventId && eid <= passes[i].endEventId)
                return static_cast<int>(i);
        }
        return -1;
    };

    std::map<uint64_t, std::set<int>> writers, readers;
    for (const auto& u : usages) {
        int pi = findPass(u.eventId);
        if (pi < 0) continue;
        if (isWriteUsage(u.usage)) writers[u.resourceId].insert(pi);
        if (isReadUsage(u.usage))  readers[u.resourceId].insert(pi);
    }

    std::map<std::pair<int,int>, std::vector<uint64_t>> edgeMap;
    for (const auto& [rid, ws] : writers) {
        auto it = readers.find(rid);
        if (it == readers.end()) continue;
        for (int w : ws) {
            for (int r : it->second) {
                if (w < r) edgeMap[{w, r}].push_back(rid);
            }
        }
    }

    std::vector<TestEdge> result;
    for (const auto& [key, rids] : edgeMap) {
        TestEdge e;
        e.src = passes[key.first].name;
        e.dst = passes[key.second].name;
        e.resources = rids;
        result.push_back(std::move(e));
    }
    return result;
}

TEST(PassAnalysisUnit, DependencyDAG_LinearChain) {
    std::vector<PassRange> passes = {
        {"PassA", 10, 20, 12, false},
        {"PassB", 30, 40, 32, false},
        {"PassC", 50, 60, 52, false},
    };
    std::vector<MockUsage> usages = {
        {100, 15, "ColorTarget"},
        {100, 35, "PS_Resource"},
        {200, 35, "ColorTarget"},
        {200, 55, "PS_Resource"},
    };
    auto edges = buildEdges(passes, usages);
    ASSERT_EQ(edges.size(), 2u);
    EXPECT_EQ(edges[0].src, "PassA");
    EXPECT_EQ(edges[0].dst, "PassB");
    EXPECT_EQ(edges[1].src, "PassB");
    EXPECT_EQ(edges[1].dst, "PassC");
}

TEST(PassAnalysisUnit, DependencyDAG_MultipleSharedResources) {
    std::vector<PassRange> passes = {
        {"Shadow", 10, 20, 12, false},
        {"Lighting", 30, 40, 32, false},
    };
    std::vector<MockUsage> usages = {
        {100, 15, "ColorTarget"},
        {200, 15, "DepthStencilTarget"},
        {100, 35, "PS_Resource"},
        {200, 35, "PS_Resource"},
    };
    auto edges = buildEdges(passes, usages);
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_EQ(edges[0].resources.size(), 2u);
}

TEST(PassAnalysisUnit, DependencyDAG_NoEdgesForSinglePass) {
    std::vector<PassRange> passes = {{"Only", 10, 50, 12, true}};
    std::vector<MockUsage> usages = {
        {100, 15, "ColorTarget"},
        {100, 35, "PS_Resource"},
    };
    auto edges = buildEdges(passes, usages);
    EXPECT_EQ(edges.size(), 0u);
}

// ---- Wave assignment tests ----

struct WaveTarget {
    uint64_t resourceId;
    std::set<int> writtenByPasses;
};

static std::map<uint64_t, uint32_t> assignWaves(
    const std::set<uint64_t>& unusedSet,
    const std::vector<WaveTarget>& targets,
    const std::map<uint64_t, std::set<int>>& readers)
{
    std::map<uint64_t, uint32_t> waveMap;
    std::set<uint64_t> remaining = unusedSet;
    uint32_t wave = 1;

    std::map<int, std::set<uint64_t>> passWrites;
    for (const auto& t : targets) {
        if (unusedSet.count(t.resourceId))
            for (int pi : t.writtenByPasses)
                passWrites[pi].insert(t.resourceId);
    }

    while (!remaining.empty()) {
        std::set<uint64_t> thisWave;
        for (auto rid : remaining) {
            bool hasRemainingConsumer = false;
            auto it = readers.find(rid);
            if (it != readers.end()) {
                for (int pi : it->second) {
                    auto pw = passWrites.find(pi);
                    if (pw != passWrites.end()) {
                        for (auto wrid : pw->second) {
                            if (remaining.count(wrid) && wrid != rid) {
                                hasRemainingConsumer = true;
                                break;
                            }
                        }
                    }
                    if (hasRemainingConsumer) break;
                }
            }
            if (!hasRemainingConsumer)
                thisWave.insert(rid);
        }
        if (thisWave.empty()) {
            for (auto rid : remaining) waveMap[rid] = wave;
            remaining.clear();
        } else {
            for (auto rid : thisWave) {
                waveMap[rid] = wave;
                remaining.erase(rid);
            }
            wave++;
        }
    }
    return waveMap;
}

TEST(PassAnalysisUnit, WaveAssignment_AllLive) {
    std::set<uint64_t> unused;
    std::vector<WaveTarget> targets = {{100, {0}}, {200, {1}}};
    std::map<uint64_t, std::set<int>> readers;
    auto waves = assignWaves(unused, targets, readers);
    EXPECT_EQ(waves.size(), 0u);
}

TEST(PassAnalysisUnit, WaveAssignment_SingleUnused) {
    std::set<uint64_t> unused = {200};
    std::vector<WaveTarget> targets = {{100, {0}}, {200, {1}}};
    std::map<uint64_t, std::set<int>> readers;
    auto waves = assignWaves(unused, targets, readers);
    ASSERT_EQ(waves.size(), 1u);
    EXPECT_EQ(waves[200], 1u);
}

TEST(PassAnalysisUnit, WaveAssignment_ChainedDead) {
    std::set<uint64_t> unused = {100, 200};
    std::vector<WaveTarget> targets = {{100, {0}}, {200, {1}}};
    std::map<uint64_t, std::set<int>> readers = {{100, {1}}};
    auto waves = assignWaves(unused, targets, readers);
    ASSERT_EQ(waves.size(), 2u);
    EXPECT_EQ(waves[200], 1u);
    EXPECT_EQ(waves[100], 2u);
}
