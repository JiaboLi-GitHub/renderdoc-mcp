#include "core/cbuffer.h"
#include "core/errors.h"
#include "core/session.h"

#include <renderdoc_replay.h>

namespace renderdoc::core {

namespace {

// Convert our ShaderStage enum to RenderDoc's ShaderStage enum.
::ShaderStage toRdcStage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:   return ::ShaderStage::Vertex;
        case ShaderStage::Hull:     return ::ShaderStage::Hull;
        case ShaderStage::Domain:   return ::ShaderStage::Domain;
        case ShaderStage::Geometry: return ::ShaderStage::Geometry;
        case ShaderStage::Pixel:    return ::ShaderStage::Pixel;
        case ShaderStage::Compute:  return ::ShaderStage::Compute;
    }
    return ::ShaderStage::Vertex;
}

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

const char* varTypeToString(VarType type) {
    switch (type) {
        case VarType::Float:            return "float";
        case VarType::Double:           return "double";
        case VarType::Half:             return "half";
        case VarType::SInt:             return "int";
        case VarType::UInt:             return "uint";
        case VarType::SShort:           return "short";
        case VarType::UShort:           return "ushort";
        case VarType::SLong:            return "long";
        case VarType::ULong:            return "ulong";
        case VarType::SByte:            return "sbyte";
        case VarType::UByte:            return "ubyte";
        case VarType::Bool:             return "bool";
        case VarType::Enum:             return "enum";
        case VarType::Struct:           return "struct";
        case VarType::GPUPointer:       return "pointer";
        case VarType::ConstantBlock:    return "cbuffer";
        case VarType::ReadOnlyResource: return "srv";
        case VarType::ReadWriteResource:return "uav";
        case VarType::Sampler:          return "sampler";
        default:                        return "unknown";
    }
}

std::string buildTypeName(const ::ShaderVariable& var) {
    std::string base = varTypeToString(var.type);
    if (!var.members.empty())
        return "struct";
    if (var.rows > 1 && var.columns > 1)
        return base + std::to_string(var.rows) + "x" + std::to_string(var.columns);
    if (var.columns > 1)
        return base + std::to_string(var.columns);
    if (var.rows > 1)
        return base + std::to_string(var.rows);
    return base;
}

ShaderVar convertShaderVar(const ::ShaderVariable& v, int depth = 0) {
    ShaderVar out;
    out.name = std::string(v.name.c_str());
    out.rows = v.rows;
    out.columns = v.columns;
    out.typeName = buildTypeName(v);

    if (!v.members.empty() && depth < 8) {
        out.members.reserve(v.members.size());
        for (const auto& m : v.members)
            out.members.push_back(convertShaderVar(m, depth + 1));
        return out;
    }

    uint32_t count = static_cast<uint32_t>(v.rows) * static_cast<uint32_t>(v.columns);
    if (count == 0) count = 1;

    switch (v.type) {
        case VarType::Float:
        case VarType::Half:
            out.floatValues.reserve(count);
            for (uint32_t i = 0; i < count && i < 16; i++)
                out.floatValues.push_back(static_cast<double>(v.value.f32v[i]));
            break;
        case VarType::Double:
            out.floatValues.reserve(count);
            for (uint32_t i = 0; i < count && i < 8; i++)
                out.floatValues.push_back(v.value.f64v[i]);
            break;
        case VarType::SInt:
        case VarType::SShort:
        case VarType::SByte:
        case VarType::SLong:
            out.intValues.reserve(count);
            for (uint32_t i = 0; i < count && i < 16; i++)
                out.intValues.push_back(static_cast<int64_t>(v.value.s32v[i]));
            break;
        case VarType::UInt:
        case VarType::UShort:
        case VarType::UByte:
        case VarType::ULong:
        case VarType::Bool:
            out.uintValues.reserve(count);
            for (uint32_t i = 0; i < count && i < 16; i++)
                out.uintValues.push_back(static_cast<uint64_t>(v.value.u32v[i]));
            break;
        default:
            out.floatValues.reserve(count);
            for (uint32_t i = 0; i < count && i < 16; i++)
                out.floatValues.push_back(static_cast<double>(v.value.f32v[i]));
            break;
    }

    return out;
}

// Per-API shader stage info: reflection, shader ResourceId, pipeline ResourceId, entry point.
struct ShaderStageInfo {
    const ::ShaderReflection* reflection = nullptr;
    ::ResourceId shaderId;
    ::ResourceId pipelineId;
    std::string entryPoint;
};

ShaderStageInfo getShaderStageInfo(IReplayController* ctrl, ShaderStage stage) {
    APIProperties props = ctrl->GetAPIProperties();
    std::string stageStr = stageToString(stage);
    ShaderStageInfo info;

    // Helper macro to reduce per-stage boilerplate
    #define EXTRACT_STAGE(apiState, shaderField) \
        info.reflection = apiState->shaderField.reflection; \
        info.shaderId = apiState->shaderField.resourceId; \
        if (info.reflection) info.entryPoint = std::string(info.reflection->entryPoint.c_str());

    #define EXTRACT_STAGE_GL(apiState, shaderField) \
        info.reflection = apiState->shaderField.reflection; \
        info.shaderId = apiState->shaderField.shaderResourceId; \
        if (info.reflection) info.entryPoint = std::string(info.reflection->entryPoint.c_str());

    switch (props.pipelineType) {
        case GraphicsAPI::D3D11: {
            const auto* s = ctrl->GetD3D11PipelineState();
            if (!s) break;
            // D3D11 has no pipeline object
            if      (stageStr == "vs") { EXTRACT_STAGE(s, vertexShader) }
            else if (stageStr == "hs") { EXTRACT_STAGE(s, hullShader) }
            else if (stageStr == "ds") { EXTRACT_STAGE(s, domainShader) }
            else if (stageStr == "gs") { EXTRACT_STAGE(s, geometryShader) }
            else if (stageStr == "ps") { EXTRACT_STAGE(s, pixelShader) }
            else if (stageStr == "cs") { EXTRACT_STAGE(s, computeShader) }
            break;
        }
        case GraphicsAPI::D3D12: {
            const auto* s = ctrl->GetD3D12PipelineState();
            if (!s) break;
            info.pipelineId = s->pipelineResourceId;
            if      (stageStr == "vs") { EXTRACT_STAGE(s, vertexShader) }
            else if (stageStr == "hs") { EXTRACT_STAGE(s, hullShader) }
            else if (stageStr == "ds") { EXTRACT_STAGE(s, domainShader) }
            else if (stageStr == "gs") { EXTRACT_STAGE(s, geometryShader) }
            else if (stageStr == "ps") { EXTRACT_STAGE(s, pixelShader) }
            else if (stageStr == "cs") { EXTRACT_STAGE(s, computeShader) }
            break;
        }
        case GraphicsAPI::OpenGL: {
            const auto* s = ctrl->GetGLPipelineState();
            if (!s) break;
            info.pipelineId = s->pipelineResourceId;
            if      (stageStr == "vs") { EXTRACT_STAGE_GL(s, vertexShader) }
            else if (stageStr == "hs") { EXTRACT_STAGE_GL(s, tessControlShader) }
            else if (stageStr == "ds") { EXTRACT_STAGE_GL(s, tessEvalShader) }
            else if (stageStr == "gs") { EXTRACT_STAGE_GL(s, geometryShader) }
            else if (stageStr == "ps") { EXTRACT_STAGE_GL(s, fragmentShader) }
            else if (stageStr == "cs") { EXTRACT_STAGE_GL(s, computeShader) }
            break;
        }
        case GraphicsAPI::Vulkan: {
            const auto* s = ctrl->GetVulkanPipelineState();
            if (!s) break;
            info.pipelineId = (stageStr == "cs") ? s->compute.pipelineResourceId
                                                  : s->graphics.pipelineResourceId;
            if      (stageStr == "vs") { EXTRACT_STAGE(s, vertexShader) }
            else if (stageStr == "hs") { EXTRACT_STAGE(s, tessControlShader) }
            else if (stageStr == "ds") { EXTRACT_STAGE(s, tessEvalShader) }
            else if (stageStr == "gs") { EXTRACT_STAGE(s, geometryShader) }
            else if (stageStr == "ps") { EXTRACT_STAGE(s, fragmentShader) }
            else if (stageStr == "cs") { EXTRACT_STAGE(s, computeShader) }
            break;
        }
        default:
            break;
    }

    #undef EXTRACT_STAGE
    #undef EXTRACT_STAGE_GL

    return info;
}

} // anonymous namespace

std::vector<CBufferInfo> listCBuffers(const Session& session,
                                       ShaderStage stage,
                                       std::optional<uint32_t> eventId) {
    auto* ctrl = session.controller();
    if (eventId.has_value())
        ctrl->SetFrameEvent(*eventId, true);

    auto stageInfo = getShaderStageInfo(ctrl, stage);
    if (!stageInfo.reflection)
        throw CoreError(CoreError::Code::NoShaderBound,
                        "No shader bound at the specified stage");

    const auto& refl = *stageInfo.reflection;
    std::vector<CBufferInfo> result;
    result.reserve(refl.constantBlocks.size());

    for (size_t i = 0; i < refl.constantBlocks.size(); i++) {
        const auto& cb = refl.constantBlocks[i];
        CBufferInfo info;
        info.index = static_cast<uint32_t>(i);
        info.name = std::string(cb.name.c_str());
        info.bindSet = cb.fixedBindSetOrSpace;
        info.bindSlot = cb.fixedBindNumber;
        info.byteSize = cb.byteSize;
        info.bufferBacked = cb.bufferBacked;
        info.variableCount = static_cast<uint32_t>(cb.variables.size());
        result.push_back(std::move(info));
    }

    return result;
}

CBufferContents getCBufferContents(const Session& session,
                                    ShaderStage stage,
                                    uint32_t cbufferIndex,
                                    std::optional<uint32_t> eventId) {
    auto* ctrl = session.controller();
    if (eventId.has_value())
        ctrl->SetFrameEvent(*eventId, true);

    auto stageInfo = getShaderStageInfo(ctrl, stage);
    if (!stageInfo.reflection)
        throw CoreError(CoreError::Code::NoShaderBound,
                        "No shader bound at the specified stage");

    const auto& refl = *stageInfo.reflection;
    if (cbufferIndex >= refl.constantBlocks.size())
        throw CoreError(CoreError::Code::InternalError,
                        "Constant block index " + std::to_string(cbufferIndex) +
                        " out of range (max " + std::to_string(refl.constantBlocks.size()) + ")");

    const auto& cbMeta = refl.constantBlocks[cbufferIndex];

    // Resolve the actual buffer binding via the descriptor access system.
    // On OpenGL, UBO bindings are dynamic and cannot be inferred from shader
    // reflection alone — we query GetDescriptorAccess() to find the descriptor
    // that backs this constant block, then GetDescriptors() to get the actual
    // buffer ResourceId, offset, and size.
    ::ShaderStage rdcStage = toRdcStage(stage);
    rdcstr entryPoint(stageInfo.entryPoint.c_str());

    ::ResourceId cbBufferId;
    uint64_t cbByteOffset = 0;
    uint64_t cbByteSize = 0;

    const auto& accesses = ctrl->GetDescriptorAccess();
    for (int i = 0; i < accesses.count(); i++) {
        const auto& access = accesses[i];
        if (access.stage == rdcStage &&
            IsConstantBlockDescriptor(access.type) &&
            access.index == static_cast<uint16_t>(cbufferIndex) &&
            access.arrayElement == 0) {
            // Found the descriptor access — now fetch the descriptor contents.
            rdcarray<DescriptorRange> ranges;
            ranges.push_back(DescriptorRange(access));
            auto descriptors = ctrl->GetDescriptors(access.descriptorStore, ranges);
            if (!descriptors.empty()) {
                cbBufferId  = descriptors[0].resource;
                cbByteOffset = descriptors[0].byteOffset;
                cbByteSize   = descriptors[0].byteSize;
            }
            break;
        }
    }

    rdcarray<::ShaderVariable> vars = ctrl->GetCBufferVariableContents(
        stageInfo.pipelineId, stageInfo.shaderId, rdcStage,
        entryPoint, cbufferIndex,
        cbBufferId, cbByteOffset, cbByteSize);

    // Build result
    CBufferContents result;
    result.eventId = eventId.value_or(session.currentEventId());
    result.stage = stage;
    result.bindSet = cbMeta.fixedBindSetOrSpace;
    result.bindSlot = cbMeta.fixedBindNumber;
    result.blockName = std::string(cbMeta.name.c_str());
    result.byteSize = cbMeta.byteSize;

    result.variables.reserve(vars.size());
    for (const auto& v : vars)
        result.variables.push_back(convertShaderVar(v));

    return result;
}

} // namespace renderdoc::core
