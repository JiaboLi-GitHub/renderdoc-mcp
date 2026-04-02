#pragma once

#include "core/types.h"
#include <optional>
#include <string>
#include <vector>

namespace renderdoc::core {

class DiffSession;

enum class DiffStatus { Equal, Modified, Added, Deleted };

struct DrawRecord {
    uint32_t eventId = 0;
    std::string drawType;
    std::string markerPath;
    uint64_t triangles = 0;
    uint32_t instances = 0;
    std::string passName;
    std::string shaderHash;
    std::string topology;
};

struct DrawDiffRow {
    DiffStatus status = DiffStatus::Equal;
    std::optional<DrawRecord> a;
    std::optional<DrawRecord> b;
    std::string confidence;
};

struct DrawsDiffResult {
    std::vector<DrawDiffRow> rows;
    int added = 0, deleted = 0, modified = 0, unchanged = 0;
};

struct ResourceDiffRow {
    DiffStatus status = DiffStatus::Equal;
    std::string name;
    std::string typeA, typeB;
    std::string confidence;
};

struct ResourcesDiffResult {
    std::vector<ResourceDiffRow> rows;
    int added = 0, deleted = 0, modified = 0, unchanged = 0;
};

struct PassDiffRow {
    DiffStatus status = DiffStatus::Equal;
    std::string name;
    std::optional<uint32_t> drawsA, drawsB;
    std::optional<uint64_t> trianglesA, trianglesB;
    std::optional<uint32_t> dispatchesA, dispatchesB;
};

struct StatsDiffResult {
    std::vector<PassDiffRow> rows;
    int passesChanged = 0, passesAdded = 0, passesDeleted = 0;
    int64_t drawsDelta = 0, trianglesDelta = 0, dispatchesDelta = 0;
};

struct PipeFieldDiff {
    std::string section;
    std::string field;
    std::string valueA;
    std::string valueB;
    bool changed = false;
};

struct PipelineDiffResult {
    uint32_t eidA = 0, eidB = 0;
    std::string markerPath;
    std::vector<PipeFieldDiff> fields;
    int changedCount = 0, totalCount = 0;
};

struct SummaryRow {
    std::string category;
    int valueA = 0, valueB = 0;
    int delta = 0;
};

struct SummaryDiffResult {
    std::vector<SummaryRow> rows;
    bool identical = false;
    std::string divergedAt;
};

SummaryDiffResult    diffSummary(DiffSession& session);
DrawsDiffResult      diffDraws(DiffSession& session);
ResourcesDiffResult  diffResources(DiffSession& session);
StatsDiffResult      diffStats(DiffSession& session);
PipelineDiffResult   diffPipeline(DiffSession& session, const std::string& markerPath);
ImageCompareResult   diffFramebuffer(DiffSession& session,
                                      uint32_t eidA = 0, uint32_t eidB = 0,
                                      int target = 0,
                                      double threshold = 0.0,
                                      const std::string& diffOutput = "");

using AlignedPair = std::pair<std::optional<size_t>, std::optional<size_t>>;
std::vector<AlignedPair> lcsAlign(const std::vector<std::string>& keysA,
                                   const std::vector<std::string>& keysB);

std::string makeDrawMatchKey(const DrawRecord& rec, bool hasMarkers);

} // namespace renderdoc::core
