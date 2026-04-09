#pragma once

#include "core/diff.h"
#include "core/diff_session.h"
#include "core/resource_id.h"

#include <renderdoc_replay.h>

#include <string>
#include <vector>

namespace renderdoc::core {
namespace diff_internal {

std::string toLower(const std::string& s);
std::string actionTypeString(ActionFlags flags);
std::string topologyString(Topology topo);
std::string shaderIdHash(::ResourceId id);
uint64_t computeTriangles(uint32_t numIndices, uint32_t numInstances);
std::string stageName(ShaderStage stage);

void buildDrawRecords(const ActionDescription& action,
                      const std::string& parentPath,
                      std::vector<DrawRecord>& out);
std::vector<DrawRecord> collectDrawRecords(IReplayController* ctrl);
bool hasAnyMarker(const std::vector<DrawRecord>& records);

const ActionDescription* findLastDraw(const rdcarray<ActionDescription>& actions);
uint32_t countEvents(const rdcarray<ActionDescription>& actions);
uint32_t countDraws(const rdcarray<ActionDescription>& actions);
uint32_t countPasses(const rdcarray<ActionDescription>& actions);

} // namespace diff_internal
} // namespace renderdoc::core
