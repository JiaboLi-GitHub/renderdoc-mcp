#include "core/shader_edit.h"
#include "core/errors.h"
#include "core/session.h"

#include <renderdoc_replay.h>

#include <algorithm>
#include <cstring>

namespace renderdoc::core {

namespace {

// Convert RenderDoc ResourceId to our uint64_t alias.
core::ResourceId toResourceId(::ResourceId id) {
    static_assert(sizeof(::ResourceId) == sizeof(uint64_t), "ResourceId size mismatch");
    uint64_t raw = 0;
    std::memcpy(&raw, &id, sizeof(raw));
    return raw;
}

// Convert our uint64_t alias back to RenderDoc ResourceId.
::ResourceId fromResourceId(uint64_t raw) {
    static_assert(sizeof(::ResourceId) == sizeof(uint64_t), "ResourceId size mismatch");
    ::ResourceId id;
    std::memcpy(&id, &raw, sizeof(id));
    return id;
}

// Map our ShaderStage enum to RenderDoc's ::ShaderStage enum.
::ShaderStage toRdcShaderStage(ShaderStage stage) {
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

// Convert ShaderEncoding enum to string.
std::string shaderEncodingToString(::ShaderEncoding enc) {
    switch (enc) {
        case ::ShaderEncoding::Unknown:       return "Unknown";
        case ::ShaderEncoding::DXBC:          return "DXBC";
        case ::ShaderEncoding::GLSL:          return "GLSL";
        case ::ShaderEncoding::SPIRV:         return "SPIRV";
        case ::ShaderEncoding::SPIRVAsm:      return "SPIRVAsm";
        case ::ShaderEncoding::HLSL:          return "HLSL";
        case ::ShaderEncoding::DXIL:          return "DXIL";
        case ::ShaderEncoding::OpenGLSPIRV:   return "OpenGLSPIRV";
        case ::ShaderEncoding::OpenGLSPIRVAsm:return "OpenGLSPIRVAsm";
        case ::ShaderEncoding::Slang:         return "Slang";
        default:                              return "Unknown";
    }
}

// Parse string to ShaderEncoding enum.
::ShaderEncoding parseShaderEncoding(const std::string& s) {
    if (s == "DXBC")          return ::ShaderEncoding::DXBC;
    if (s == "GLSL")          return ::ShaderEncoding::GLSL;
    if (s == "SPIRV")         return ::ShaderEncoding::SPIRV;
    if (s == "SPIRVAsm")      return ::ShaderEncoding::SPIRVAsm;
    if (s == "HLSL")          return ::ShaderEncoding::HLSL;
    if (s == "DXIL")          return ::ShaderEncoding::DXIL;
    if (s == "OpenGLSPIRV")   return ::ShaderEncoding::OpenGLSPIRV;
    if (s == "OpenGLSPIRVAsm")return ::ShaderEncoding::OpenGLSPIRVAsm;
    if (s == "Slang")         return ::ShaderEncoding::Slang;
    return ::ShaderEncoding::Unknown;
}

// Get the shader ResourceId for a given stage from the current pipeline state.
::ResourceId getShaderResourceIdForStage(IReplayController* ctrl, ShaderStage stage) {
    APIProperties props = ctrl->GetAPIProperties();

    switch (props.pipelineType) {
        case GraphicsAPI::D3D11: {
            const auto* state = ctrl->GetD3D11PipelineState();
            if (!state) return ::ResourceId::Null();
            switch (stage) {
                case ShaderStage::Vertex:   return state->vertexShader.resourceId;
                case ShaderStage::Hull:     return state->hullShader.resourceId;
                case ShaderStage::Domain:   return state->domainShader.resourceId;
                case ShaderStage::Geometry: return state->geometryShader.resourceId;
                case ShaderStage::Pixel:    return state->pixelShader.resourceId;
                case ShaderStage::Compute:  return state->computeShader.resourceId;
            }
            break;
        }
        case GraphicsAPI::D3D12: {
            const auto* state = ctrl->GetD3D12PipelineState();
            if (!state) return ::ResourceId::Null();
            switch (stage) {
                case ShaderStage::Vertex:   return state->vertexShader.resourceId;
                case ShaderStage::Hull:     return state->hullShader.resourceId;
                case ShaderStage::Domain:   return state->domainShader.resourceId;
                case ShaderStage::Geometry: return state->geometryShader.resourceId;
                case ShaderStage::Pixel:    return state->pixelShader.resourceId;
                case ShaderStage::Compute:  return state->computeShader.resourceId;
            }
            break;
        }
        case GraphicsAPI::OpenGL: {
            const auto* state = ctrl->GetGLPipelineState();
            if (!state) return ::ResourceId::Null();
            switch (stage) {
                case ShaderStage::Vertex:   return state->vertexShader.shaderResourceId;
                case ShaderStage::Hull:     return state->tessControlShader.shaderResourceId;
                case ShaderStage::Domain:   return state->tessEvalShader.shaderResourceId;
                case ShaderStage::Geometry: return state->geometryShader.shaderResourceId;
                case ShaderStage::Pixel:    return state->fragmentShader.shaderResourceId;
                case ShaderStage::Compute:  return state->computeShader.shaderResourceId;
            }
            break;
        }
        case GraphicsAPI::Vulkan: {
            const auto* state = ctrl->GetVulkanPipelineState();
            if (!state) return ::ResourceId::Null();
            switch (stage) {
                case ShaderStage::Vertex:   return state->vertexShader.resourceId;
                case ShaderStage::Hull:     return state->tessControlShader.resourceId;
                case ShaderStage::Domain:   return state->tessEvalShader.resourceId;
                case ShaderStage::Geometry: return state->geometryShader.resourceId;
                case ShaderStage::Pixel:    return state->fragmentShader.resourceId;
                case ShaderStage::Compute:  return state->computeShader.resourceId;
            }
            break;
        }
        default:
            break;
    }
    return ::ResourceId::Null();
}

} // anonymous namespace

std::vector<std::string> getShaderEncodings(const Session& session) {
    auto* ctrl = session.controller();
    rdcarray<::ShaderEncoding> encodings = ctrl->GetTargetShaderEncodings();

    std::vector<std::string> result;
    result.reserve(encodings.count());
    for (int i = 0; i < encodings.count(); i++) {
        result.push_back(shaderEncodingToString(encodings[i]));
    }
    return result;
}

ShaderBuildResult buildShader(Session& session,
                              const std::string& source,
                              ShaderStage stage,
                              const std::string& entry,
                              const std::string& encoding) {
    auto* ctrl = session.controller();

    ::ShaderEncoding enc = parseShaderEncoding(encoding);
    if (enc == ::ShaderEncoding::Unknown)
        throw CoreError(CoreError::Code::InternalError,
                        "Unknown shader encoding: " + encoding);

    bytebuf sourceBytes;
    sourceBytes.assign((const byte*)source.data(), source.size());

    ShaderCompileFlags flags;

    rdcpair<::ResourceId, rdcstr> result =
        ctrl->BuildTargetShader(rdcstr(entry.c_str()), enc, sourceBytes, flags,
                                toRdcShaderStage(stage));

    ShaderBuildResult buildResult;
    uint64_t rawId = toResourceId(result.first);

    if (result.first != ::ResourceId::Null()) {
        buildResult.shaderId = rawId;
        // Track the built shader for cleanup
        ShaderEditState& state = session.shaderEditState();
        state.builtShaders[rawId] = rawId;
    }

    buildResult.warnings = std::string(result.second.c_str());
    return buildResult;
}

uint64_t replaceShader(Session& session,
                       uint32_t eventId,
                       ShaderStage stage,
                       uint64_t shaderId) {
    auto* ctrl = session.controller();

    // Navigate to the event to get the original shader
    ctrl->SetFrameEvent(eventId, true);

    ::ResourceId originalRdc = getShaderResourceIdForStage(ctrl, stage);
    if (originalRdc == ::ResourceId::Null())
        throw CoreError(CoreError::Code::InternalError,
                        "No shader bound at the specified stage for event " +
                        std::to_string(eventId));

    uint64_t originalId = toResourceId(originalRdc);
    ::ResourceId replacementRdc = fromResourceId(shaderId);

    ctrl->ReplaceResource(originalRdc, replacementRdc);

    // Track the replacement for cleanup
    ShaderEditState& state = session.shaderEditState();
    state.shaderReplacements[originalId] = shaderId;

    return originalId;
}

void restoreShader(Session& session,
                   uint32_t eventId,
                   ShaderStage stage) {
    auto* ctrl = session.controller();

    ctrl->SetFrameEvent(eventId, true);

    ::ResourceId originalRdc = getShaderResourceIdForStage(ctrl, stage);
    if (originalRdc == ::ResourceId::Null())
        throw CoreError(CoreError::Code::InternalError,
                        "No shader bound at the specified stage for event " +
                        std::to_string(eventId));

    uint64_t originalId = toResourceId(originalRdc);
    ShaderEditState& state = session.shaderEditState();

    auto it = state.shaderReplacements.find(originalId);
    if (it == state.shaderReplacements.end())
        throw CoreError(CoreError::Code::InternalError,
                        "No replacement found for shader at the specified stage");

    ctrl->RemoveReplacement(originalRdc);
    state.shaderReplacements.erase(it);
}

std::pair<int, int> restoreAllShaders(Session& session) {
    auto* ctrl = session.controller();
    ShaderEditState& state = session.shaderEditState();

    int restored = 0;
    int errors = 0;

    // Copy the map since we modify it during iteration
    auto replacements = state.shaderReplacements;
    for (const auto& [originalId, replacementId] : replacements) {
        try {
            ::ResourceId originalRdc = fromResourceId(originalId);
            ctrl->RemoveReplacement(originalRdc);
            state.shaderReplacements.erase(originalId);
            restored++;
        } catch (...) {
            errors++;
        }
    }

    return {restored, errors};
}

void cleanupShaderEdits(Session& session) {
    auto* ctrl = session.controller();
    ShaderEditState& state = session.shaderEditState();

    // Remove all replacements first
    for (const auto& [originalId, replacementId] : state.shaderReplacements) {
        try {
            ::ResourceId originalRdc = fromResourceId(originalId);
            ctrl->RemoveReplacement(originalRdc);
        } catch (...) {
            // Best-effort cleanup
        }
    }
    state.shaderReplacements.clear();

    // Free all built shaders
    for (const auto& [id, rawId] : state.builtShaders) {
        try {
            ::ResourceId rdc = fromResourceId(rawId);
            ctrl->FreeTargetResource(rdc);
        } catch (...) {
            // Best-effort cleanup
        }
    }
    state.builtShaders.clear();
}

} // namespace renderdoc::core
