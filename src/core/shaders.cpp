#include "core/shaders.h"
#include "core/errors.h"
#include "core/resource_id.h"
#include "core/session.h"

#include <renderdoc_replay.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace renderdoc::core {

namespace {

std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

// Convert ShaderStage enum to the short string used for API lookups.
std::string stageToString(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:   return "vs";
        case ShaderStage::Hull:     return "hs";
        case ShaderStage::Domain:   return "ds";
        case ShaderStage::Geometry: return "gs";
        case ShaderStage::Pixel:    return "ps";
        case ShaderStage::Compute:  return "cs";
    }
    return "vs";
}

// Helper struct pairing the RenderDoc ShaderReflection pointer with its resource ID.
struct ShaderStageInfo {
    const ::ShaderReflection* reflection = nullptr;
    ::ResourceId resourceId;
};

// Get shader info for a given stage across all supported APIs.
ShaderStageInfo getShaderStageInfo(IReplayController* ctrl, const std::string& stage) {
    APIProperties props = ctrl->GetAPIProperties();
    ShaderStageInfo info;

    switch (props.pipelineType) {
        case GraphicsAPI::D3D11: {
            const auto* state = ctrl->GetD3D11PipelineState();
            if (!state) break;
            if      (stage == "vs") { info.reflection = state->vertexShader.reflection;   info.resourceId = state->vertexShader.resourceId; }
            else if (stage == "hs") { info.reflection = state->hullShader.reflection;     info.resourceId = state->hullShader.resourceId; }
            else if (stage == "ds") { info.reflection = state->domainShader.reflection;   info.resourceId = state->domainShader.resourceId; }
            else if (stage == "gs") { info.reflection = state->geometryShader.reflection; info.resourceId = state->geometryShader.resourceId; }
            else if (stage == "ps") { info.reflection = state->pixelShader.reflection;    info.resourceId = state->pixelShader.resourceId; }
            else if (stage == "cs") { info.reflection = state->computeShader.reflection;  info.resourceId = state->computeShader.resourceId; }
            break;
        }
        case GraphicsAPI::D3D12: {
            const auto* state = ctrl->GetD3D12PipelineState();
            if (!state) break;
            if      (stage == "vs") { info.reflection = state->vertexShader.reflection;   info.resourceId = state->vertexShader.resourceId; }
            else if (stage == "hs") { info.reflection = state->hullShader.reflection;     info.resourceId = state->hullShader.resourceId; }
            else if (stage == "ds") { info.reflection = state->domainShader.reflection;   info.resourceId = state->domainShader.resourceId; }
            else if (stage == "gs") { info.reflection = state->geometryShader.reflection; info.resourceId = state->geometryShader.resourceId; }
            else if (stage == "ps") { info.reflection = state->pixelShader.reflection;    info.resourceId = state->pixelShader.resourceId; }
            else if (stage == "cs") { info.reflection = state->computeShader.reflection;  info.resourceId = state->computeShader.resourceId; }
            break;
        }
        case GraphicsAPI::OpenGL: {
            const auto* state = ctrl->GetGLPipelineState();
            if (!state) break;
            if      (stage == "vs") { info.reflection = state->vertexShader.reflection;    info.resourceId = state->vertexShader.shaderResourceId; }
            else if (stage == "hs") { info.reflection = state->tessControlShader.reflection; info.resourceId = state->tessControlShader.shaderResourceId; }
            else if (stage == "ds") { info.reflection = state->tessEvalShader.reflection;  info.resourceId = state->tessEvalShader.shaderResourceId; }
            else if (stage == "gs") { info.reflection = state->geometryShader.reflection;  info.resourceId = state->geometryShader.shaderResourceId; }
            else if (stage == "ps") { info.reflection = state->fragmentShader.reflection;  info.resourceId = state->fragmentShader.shaderResourceId; }
            else if (stage == "cs") { info.reflection = state->computeShader.reflection;   info.resourceId = state->computeShader.shaderResourceId; }
            break;
        }
        case GraphicsAPI::Vulkan: {
            const auto* state = ctrl->GetVulkanPipelineState();
            if (!state) break;
            if      (stage == "vs") { info.reflection = state->vertexShader.reflection;      info.resourceId = state->vertexShader.resourceId; }
            else if (stage == "hs") { info.reflection = state->tessControlShader.reflection; info.resourceId = state->tessControlShader.resourceId; }
            else if (stage == "ds") { info.reflection = state->tessEvalShader.reflection;    info.resourceId = state->tessEvalShader.resourceId; }
            else if (stage == "gs") { info.reflection = state->geometryShader.reflection;    info.resourceId = state->geometryShader.resourceId; }
            else if (stage == "ps") { info.reflection = state->fragmentShader.reflection;    info.resourceId = state->fragmentShader.resourceId; }
            else if (stage == "cs") { info.reflection = state->computeShader.reflection;     info.resourceId = state->computeShader.resourceId; }
            break;
        }
        default:
            break;
    }

    return info;
}

// Key for tracking unique shaders: raw resource ID + stage string.
struct ShaderKey {
    uint64_t rawId;
    std::string stage;

    bool operator<(const ShaderKey& o) const {
        if (rawId != o.rawId) return rawId < o.rawId;
        return stage < o.stage;
    }
};

struct ShaderRecord {
    ::ResourceId resourceId;
    std::string stage;
    std::string entryPoint;
    uint32_t usageCount = 0;
    uint32_t firstEventId = 0;
};

static const char* kAllStages[] = {"vs", "hs", "ds", "gs", "ps", "cs"};

// Collect draw/dispatch event IDs recursively (up to limit).
void collectActionEvents(const rdcarray<ActionDescription>& actions,
                         std::vector<uint32_t>& eventIds,
                         int limit) {
    for (const auto& action : actions) {
        if ((int)eventIds.size() >= limit) return;
        if (bool(action.flags & ActionFlags::Drawcall) ||
            bool(action.flags & ActionFlags::Dispatch)) {
            eventIds.push_back(action.eventId);
        }
        if (!action.children.empty())
            collectActionEvents(action.children, eventIds, limit);
    }
}

// Scan up to 10000 events and collect unique shaders (up to maxUniqueShaders).
std::map<ShaderKey, ShaderRecord> collectUniqueShaders(IReplayController* ctrl,
                                                        int maxUniqueShaders) {
    const auto& rootActions = ctrl->GetRootActions();

    std::vector<uint32_t> eventIds;
    collectActionEvents(rootActions, eventIds, 10000);

    std::map<ShaderKey, ShaderRecord> shaders;

    for (uint32_t eid : eventIds) {
        if ((int)shaders.size() >= maxUniqueShaders) break;

        ctrl->SetFrameEvent(eid, true);

        for (const char* stageName : kAllStages) {
            ShaderStageInfo si = getShaderStageInfo(ctrl, stageName);
            if (!si.reflection || si.resourceId == ::ResourceId::Null())
                continue;

            uint64_t raw = toResourceId(si.resourceId);
            ShaderKey key{raw, stageName};

            auto it = shaders.find(key);
            if (it != shaders.end()) {
                it->second.usageCount++;
            } else {
                if ((int)shaders.size() >= maxUniqueShaders) continue;
                ShaderRecord rec;
                rec.resourceId  = si.resourceId;
                rec.stage       = stageName;
                rec.entryPoint  = std::string(si.reflection->entryPoint.c_str());
                rec.usageCount  = 1;
                rec.firstEventId = eid;
                shaders[key] = rec;
            }
        }
    }

    return shaders;
}

} // anonymous namespace

// ---------------------------------------------------------------------------

ShaderReflection getShaderReflection(const Session& session,
                                      ShaderStage stage,
                                      std::optional<uint32_t> eventId) {
    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open

    if (eventId)
        ctrl->SetFrameEvent(*eventId, true);

    std::string stageName = stageToString(stage);
    ShaderStageInfo si = getShaderStageInfo(ctrl, stageName);
    if (!si.reflection)
        throw CoreError(CoreError::Code::InternalError,
                        "No shader bound at stage '" + stageName + "' for the current event.");

    const ::ShaderReflection* refl = si.reflection;

    ShaderReflection result;
    result.id         = toResourceId(si.resourceId);
    result.stage      = stage;
    result.entryPoint = std::string(refl->entryPoint.c_str());

    for (int i = 0; i < refl->inputSignature.count(); i++) {
        const auto& sig = refl->inputSignature[i];
        SignatureElement se;
        se.varName       = std::string(sig.varName.c_str());
        se.semanticName  = std::string(sig.semanticName.c_str());
        se.semanticIndex = sig.semanticIndex;
        se.regIndex      = sig.regIndex;
        result.inputSignature.push_back(std::move(se));
    }

    for (int i = 0; i < refl->outputSignature.count(); i++) {
        const auto& sig = refl->outputSignature[i];
        SignatureElement se;
        se.varName       = std::string(sig.varName.c_str());
        se.semanticName  = std::string(sig.semanticName.c_str());
        se.semanticIndex = sig.semanticIndex;
        se.regIndex      = sig.regIndex;
        result.outputSignature.push_back(std::move(se));
    }

    for (int i = 0; i < refl->constantBlocks.count(); i++) {
        const auto& cb = refl->constantBlocks[i];
        ConstantBlock block;
        block.name          = std::string(cb.name.c_str());
        block.bindPoint     = cb.fixedBindNumber;
        block.byteSize      = cb.byteSize;
        block.variableCount = (uint32_t)cb.variables.count();
        result.constantBlocks.push_back(std::move(block));
    }

    for (int i = 0; i < refl->readOnlyResources.count(); i++) {
        const auto& r = refl->readOnlyResources[i];
        ShaderBindingDetail detail;
        detail.name      = std::string(r.name.c_str());
        detail.bindPoint = r.fixedBindNumber;
        result.readOnlyResources.push_back(std::move(detail));
    }

    for (int i = 0; i < refl->readWriteResources.count(); i++) {
        const auto& r = refl->readWriteResources[i];
        ShaderBindingDetail detail;
        detail.name      = std::string(r.name.c_str());
        detail.bindPoint = r.fixedBindNumber;
        result.readWriteResources.push_back(std::move(detail));
    }

    return result;
}

// ---------------------------------------------------------------------------

ShaderDisassembly getShaderDisassembly(const Session& session,
                                        ShaderStage stage,
                                        std::optional<uint32_t> eventId) {
    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open

    if (eventId)
        ctrl->SetFrameEvent(*eventId, true);

    std::string stageName = stageToString(stage);
    ShaderStageInfo si = getShaderStageInfo(ctrl, stageName);
    if (!si.reflection)
        throw CoreError(CoreError::Code::InternalError,
                        "No shader bound at stage '" + stageName + "' for the current event.");

    rdcarray<rdcstr> targets = ctrl->GetDisassemblyTargets(true);
    if (targets.isEmpty())
        throw CoreError(CoreError::Code::InternalError, "No disassembly targets available.");

    rdcstr disasmText = ctrl->DisassembleShader(::ResourceId(), si.reflection, targets[0]);

    ShaderDisassembly result;
    result.id          = toResourceId(si.resourceId);
    result.stage       = stage;
    result.disassembly = std::string(disasmText.c_str());
    result.target      = std::string(targets[0].c_str());
    return result;
}

// ---------------------------------------------------------------------------

std::vector<ShaderUsageInfo> listShaders(const Session& session) {
    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open

    auto shaders = collectUniqueShaders(ctrl, 100);

    std::vector<ShaderUsageInfo> result;
    result.reserve(shaders.size());

    for (const auto& pair : shaders) {
        const ShaderRecord& rec = pair.second;

        ShaderUsageInfo info;
        info.shaderId   = toResourceId(rec.resourceId);
        info.entryPoint = rec.entryPoint;
        info.usageCount = rec.usageCount;

        // Map stage string back to enum.
        if      (rec.stage == "vs") info.stage = ShaderStage::Vertex;
        else if (rec.stage == "hs") info.stage = ShaderStage::Hull;
        else if (rec.stage == "ds") info.stage = ShaderStage::Domain;
        else if (rec.stage == "gs") info.stage = ShaderStage::Geometry;
        else if (rec.stage == "ps") info.stage = ShaderStage::Pixel;
        else if (rec.stage == "cs") info.stage = ShaderStage::Compute;
        else                         info.stage = ShaderStage::Vertex;

        result.push_back(std::move(info));
    }

    return result;
}

// ---------------------------------------------------------------------------

std::vector<ShaderSearchMatch> searchShaders(const Session& session,
                                              const std::string& pattern,
                                              std::optional<ShaderStage> stageFilter,
                                              uint32_t limit) {
    if (pattern.empty())
        throw CoreError(CoreError::Code::InternalError, "Search pattern must not be empty.");

    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open

    const std::string lowerPattern = toLower(pattern);

    auto shaders = collectUniqueShaders(ctrl, 100);

    rdcarray<rdcstr> targets = ctrl->GetDisassemblyTargets(true);
    if (targets.isEmpty())
        throw CoreError(CoreError::Code::InternalError, "No disassembly targets available.");

    std::vector<ShaderSearchMatch> result;

    for (const auto& pair : shaders) {
        if ((uint32_t)result.size() >= limit) break;

        const ShaderRecord& rec = pair.second;

        // Optional stage filter.
        if (stageFilter) {
            const std::string& expected = stageToString(*stageFilter);
            if (rec.stage != expected) continue;
        }

        // Navigate to a known event where this shader is used.
        ctrl->SetFrameEvent(rec.firstEventId, true);
        ShaderStageInfo si = getShaderStageInfo(ctrl, rec.stage);
        if (!si.reflection) continue;

        rdcstr disasmRdc = ctrl->DisassembleShader(::ResourceId(), si.reflection, targets[0]);
        const std::string disasmStr(disasmRdc.c_str());

        // Quick case-insensitive check before line scanning.
        const std::string lowerDisasm = toLower(disasmStr);
        if (lowerDisasm.find(lowerPattern) == std::string::npos)
            continue;

        ShaderSearchMatch match;
        match.shaderId   = toResourceId(rec.resourceId);
        match.entryPoint = rec.entryPoint;

        if      (rec.stage == "vs") match.stage = ShaderStage::Vertex;
        else if (rec.stage == "hs") match.stage = ShaderStage::Hull;
        else if (rec.stage == "ds") match.stage = ShaderStage::Domain;
        else if (rec.stage == "gs") match.stage = ShaderStage::Geometry;
        else if (rec.stage == "ps") match.stage = ShaderStage::Pixel;
        else if (rec.stage == "cs") match.stage = ShaderStage::Compute;
        else                         match.stage = ShaderStage::Vertex;

        // Collect up to 10 matching lines.
        std::istringstream stream(disasmStr);
        std::string line;
        uint32_t lineNum    = 0;
        int      matchedLines = 0;

        while (std::getline(stream, line) && matchedLines < 10) {
            lineNum++;
            if (toLower(line).find(lowerPattern) != std::string::npos) {
                ShaderSearchMatch::MatchLine ml;
                ml.line = lineNum;
                ml.text = line;
                match.matchingLines.push_back(std::move(ml));
                matchedLines++;
            }
        }

        result.push_back(std::move(match));
    }

    return result;
}

} // namespace renderdoc::core
