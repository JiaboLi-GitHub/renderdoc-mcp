#include "core/pipeline.h"
#include "core/errors.h"
#include "core/session.h"

#include <renderdoc_replay.h>

#include <cstring>

namespace renderdoc::core {

namespace {

// Convert RenderDoc ResourceId struct to our uint64_t alias.
core::ResourceId toResourceId(::ResourceId id) {
    static_assert(sizeof(::ResourceId) == sizeof(uint64_t), "ResourceId size mismatch");
    uint64_t raw = 0;
    std::memcpy(&raw, &id, sizeof(raw));
    return raw;
}

// Extract StageBindings from a RenderDoc ShaderReflection pointer and resource ID.
StageBindings extractStageBindings(const ::ShaderReflection* refl, ::ResourceId resourceId) {
    StageBindings bindings;
    bindings.shaderId = toResourceId(resourceId);
    if (!refl)
        return bindings;

    for (int i = 0; i < refl->constantBlocks.count(); i++) {
        const auto& cb = refl->constantBlocks[i];
        ShaderBindingDetail detail;
        detail.name = cb.name.c_str();
        detail.bindPoint = cb.fixedBindNumber;
        detail.byteSize = cb.byteSize;
        detail.variableCount = static_cast<uint32_t>(cb.variables.count());
        bindings.constantBuffers.push_back(std::move(detail));
    }

    for (int i = 0; i < refl->readOnlyResources.count(); i++) {
        const auto& srv = refl->readOnlyResources[i];
        ShaderBindingDetail detail;
        detail.name = srv.name.c_str();
        detail.bindPoint = srv.fixedBindNumber;
        bindings.readOnlyResources.push_back(std::move(detail));
    }

    for (int i = 0; i < refl->readWriteResources.count(); i++) {
        const auto& uav = refl->readWriteResources[i];
        ShaderBindingDetail detail;
        detail.name = uav.name.c_str();
        detail.bindPoint = uav.fixedBindNumber;
        bindings.readWriteResources.push_back(std::move(detail));
    }

    for (int i = 0; i < refl->samplers.count(); i++) {
        const auto& samp = refl->samplers[i];
        ShaderBindingDetail detail;
        detail.name = samp.name.c_str();
        detail.bindPoint = samp.fixedBindNumber;
        bindings.samplers.push_back(std::move(detail));
    }

    return bindings;
}

} // anonymous namespace

PipelineState getPipelineState(const Session& session,
                                std::optional<uint32_t> eventId) {
    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open

    if (eventId.has_value())
        ctrl->SetFrameEvent(*eventId, true);

    // Cache texture & resource lists for RT info lookups.
    const auto& textures  = ctrl->GetTextures();
    const auto& resources = ctrl->GetResources();

    auto fillRTInfo = [&](RenderTargetInfo& rti) {
        uint64_t rawId = rti.id;
        for (int i = 0; i < textures.count(); i++) {
            uint64_t tid = 0;
            std::memcpy(&tid, &textures[i].resourceId, sizeof(tid));
            if (tid == rawId) {
                rti.width  = textures[i].width;
                rti.height = textures[i].height;
                break;
            }
        }
        for (int i = 0; i < resources.count(); i++) {
            uint64_t rid = 0;
            std::memcpy(&rid, &resources[i].resourceId, sizeof(rid));
            if (rid == rawId) {
                rti.name = resources[i].name.c_str();
                break;
            }
        }
    };

    APIProperties props = ctrl->GetAPIProperties();

    PipelineState state;

    switch (props.pipelineType) {
        case GraphicsAPI::D3D11: {
            state.api = GraphicsApi::D3D11;
            const D3D11Pipe::State* ps = ctrl->GetD3D11PipelineState();
            if (!ps) break;

            // Vertex Shader
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Vertex;
                sb.shaderId = toResourceId(ps->vertexShader.resourceId);
                if (ps->vertexShader.reflection)
                    sb.entryPoint = ps->vertexShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Pixel Shader
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Pixel;
                sb.shaderId = toResourceId(ps->pixelShader.resourceId);
                if (ps->pixelShader.reflection)
                    sb.entryPoint = ps->pixelShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Hull Shader
            if (ps->hullShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Hull;
                sb.shaderId = toResourceId(ps->hullShader.resourceId);
                if (ps->hullShader.reflection)
                    sb.entryPoint = ps->hullShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Domain Shader
            if (ps->domainShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Domain;
                sb.shaderId = toResourceId(ps->domainShader.resourceId);
                if (ps->domainShader.reflection)
                    sb.entryPoint = ps->domainShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Geometry Shader
            if (ps->geometryShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Geometry;
                sb.shaderId = toResourceId(ps->geometryShader.resourceId);
                if (ps->geometryShader.reflection)
                    sb.entryPoint = ps->geometryShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Compute Shader
            if (ps->computeShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Compute;
                sb.shaderId = toResourceId(ps->computeShader.resourceId);
                if (ps->computeShader.reflection)
                    sb.entryPoint = ps->computeShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }

            // Render Targets
            for (size_t i = 0; i < ps->outputMerger.renderTargets.size(); i++) {
                const auto& rt = ps->outputMerger.renderTargets[i];
                if (rt.resource == ::ResourceId::Null()) continue;
                RenderTargetInfo rti;
                rti.id = toResourceId(rt.resource);
                rti.format = rt.format.Name().c_str();
                fillRTInfo(rti);
                state.renderTargets.push_back(std::move(rti));
            }

            // Depth Target
            if (ps->outputMerger.depthTarget.resource != ::ResourceId::Null()) {
                RenderTargetInfo dti;
                dti.id = toResourceId(ps->outputMerger.depthTarget.resource);
                dti.format = ps->outputMerger.depthTarget.format.Name().c_str();
                fillRTInfo(dti);
                state.depthTarget = std::move(dti);
            }

            // Viewports
            for (const auto& vp : ps->rasterizer.viewports) {
                if (!vp.enabled) continue;
                Viewport v;
                v.x = vp.x;
                v.y = vp.y;
                v.width = vp.width;
                v.height = vp.height;
                v.minDepth = vp.minDepth;
                v.maxDepth = vp.maxDepth;
                state.viewports.push_back(v);
            }
            break;
        }

        case GraphicsAPI::D3D12: {
            state.api = GraphicsApi::D3D12;
            const D3D12Pipe::State* ps = ctrl->GetD3D12PipelineState();
            if (!ps) break;

            // Vertex Shader
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Vertex;
                sb.shaderId = toResourceId(ps->vertexShader.resourceId);
                if (ps->vertexShader.reflection)
                    sb.entryPoint = ps->vertexShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Pixel Shader
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Pixel;
                sb.shaderId = toResourceId(ps->pixelShader.resourceId);
                if (ps->pixelShader.reflection)
                    sb.entryPoint = ps->pixelShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Hull Shader
            if (ps->hullShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Hull;
                sb.shaderId = toResourceId(ps->hullShader.resourceId);
                if (ps->hullShader.reflection)
                    sb.entryPoint = ps->hullShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Domain Shader
            if (ps->domainShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Domain;
                sb.shaderId = toResourceId(ps->domainShader.resourceId);
                if (ps->domainShader.reflection)
                    sb.entryPoint = ps->domainShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Geometry Shader
            if (ps->geometryShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Geometry;
                sb.shaderId = toResourceId(ps->geometryShader.resourceId);
                if (ps->geometryShader.reflection)
                    sb.entryPoint = ps->geometryShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Compute Shader
            if (ps->computeShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Compute;
                sb.shaderId = toResourceId(ps->computeShader.resourceId);
                if (ps->computeShader.reflection)
                    sb.entryPoint = ps->computeShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }

            // Render Targets
            for (size_t i = 0; i < ps->outputMerger.renderTargets.size(); i++) {
                const auto& rt = ps->outputMerger.renderTargets[i];
                if (rt.resource == ::ResourceId::Null()) continue;
                RenderTargetInfo rti;
                rti.id = toResourceId(rt.resource);
                rti.format = rt.format.Name().c_str();
                fillRTInfo(rti);
                state.renderTargets.push_back(std::move(rti));
            }

            // Depth Target
            if (ps->outputMerger.depthTarget.resource != ::ResourceId::Null()) {
                RenderTargetInfo dti;
                dti.id = toResourceId(ps->outputMerger.depthTarget.resource);
                dti.format = ps->outputMerger.depthTarget.format.Name().c_str();
                fillRTInfo(dti);
                state.depthTarget = std::move(dti);
            }

            // Viewports
            for (const auto& vp : ps->rasterizer.viewports) {
                if (!vp.enabled) continue;
                Viewport v;
                v.x = vp.x;
                v.y = vp.y;
                v.width = vp.width;
                v.height = vp.height;
                v.minDepth = vp.minDepth;
                v.maxDepth = vp.maxDepth;
                state.viewports.push_back(v);
            }
            break;
        }

        case GraphicsAPI::OpenGL: {
            state.api = GraphicsApi::OpenGL;
            const GLPipe::State* ps = ctrl->GetGLPipelineState();
            if (!ps) break;

            // GL uses shaderResourceId instead of resourceId
            // Vertex Shader
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Vertex;
                sb.shaderId = toResourceId(ps->vertexShader.shaderResourceId);
                if (ps->vertexShader.reflection)
                    sb.entryPoint = ps->vertexShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Fragment Shader (maps to Pixel)
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Pixel;
                sb.shaderId = toResourceId(ps->fragmentShader.shaderResourceId);
                if (ps->fragmentShader.reflection)
                    sb.entryPoint = ps->fragmentShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Tess Control Shader (maps to Hull)
            if (ps->tessControlShader.shaderResourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Hull;
                sb.shaderId = toResourceId(ps->tessControlShader.shaderResourceId);
                if (ps->tessControlShader.reflection)
                    sb.entryPoint = ps->tessControlShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Tess Eval Shader (maps to Domain)
            if (ps->tessEvalShader.shaderResourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Domain;
                sb.shaderId = toResourceId(ps->tessEvalShader.shaderResourceId);
                if (ps->tessEvalShader.reflection)
                    sb.entryPoint = ps->tessEvalShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Geometry Shader
            if (ps->geometryShader.shaderResourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Geometry;
                sb.shaderId = toResourceId(ps->geometryShader.shaderResourceId);
                if (ps->geometryShader.reflection)
                    sb.entryPoint = ps->geometryShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Compute Shader
            if (ps->computeShader.shaderResourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Compute;
                sb.shaderId = toResourceId(ps->computeShader.shaderResourceId);
                if (ps->computeShader.reflection)
                    sb.entryPoint = ps->computeShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }

            // Render Targets (GL: color attachments on draw FBO, no format)
            for (size_t i = 0; i < ps->framebuffer.drawFBO.colorAttachments.size(); i++) {
                const auto& att = ps->framebuffer.drawFBO.colorAttachments[i];
                if (att.resource == ::ResourceId::Null()) continue;
                RenderTargetInfo rti;
                rti.id = toResourceId(att.resource);
                // GL does not expose a format name on color attachment at this level
                fillRTInfo(rti);
                state.renderTargets.push_back(std::move(rti));
            }

            // Depth Target (GL: depth attachment on draw FBO)
            if (ps->framebuffer.drawFBO.depthAttachment.resource != ::ResourceId::Null()) {
                RenderTargetInfo dti;
                dti.id = toResourceId(ps->framebuffer.drawFBO.depthAttachment.resource);
                fillRTInfo(dti);
                state.depthTarget = std::move(dti);
            }

            // Viewports
            for (const auto& vp : ps->rasterizer.viewports) {
                if (!vp.enabled) continue;
                Viewport v;
                v.x = vp.x;
                v.y = vp.y;
                v.width = vp.width;
                v.height = vp.height;
                v.minDepth = vp.minDepth;
                v.maxDepth = vp.maxDepth;
                state.viewports.push_back(v);
            }
            break;
        }

        case GraphicsAPI::Vulkan: {
            state.api = GraphicsApi::Vulkan;
            const VKPipe::State* ps = ctrl->GetVulkanPipelineState();
            if (!ps) break;

            // Vertex Shader
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Vertex;
                sb.shaderId = toResourceId(ps->vertexShader.resourceId);
                if (ps->vertexShader.reflection)
                    sb.entryPoint = ps->vertexShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Fragment Shader (maps to Pixel)
            {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Pixel;
                sb.shaderId = toResourceId(ps->fragmentShader.resourceId);
                if (ps->fragmentShader.reflection)
                    sb.entryPoint = ps->fragmentShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Tess Control Shader (maps to Hull)
            if (ps->tessControlShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Hull;
                sb.shaderId = toResourceId(ps->tessControlShader.resourceId);
                if (ps->tessControlShader.reflection)
                    sb.entryPoint = ps->tessControlShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Tess Eval Shader (maps to Domain)
            if (ps->tessEvalShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Domain;
                sb.shaderId = toResourceId(ps->tessEvalShader.resourceId);
                if (ps->tessEvalShader.reflection)
                    sb.entryPoint = ps->tessEvalShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Geometry Shader
            if (ps->geometryShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Geometry;
                sb.shaderId = toResourceId(ps->geometryShader.resourceId);
                if (ps->geometryShader.reflection)
                    sb.entryPoint = ps->geometryShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }
            // Compute Shader
            if (ps->computeShader.resourceId != ::ResourceId::Null()) {
                PipelineState::ShaderBinding sb;
                sb.stage = ShaderStage::Compute;
                sb.shaderId = toResourceId(ps->computeShader.resourceId);
                if (ps->computeShader.reflection)
                    sb.entryPoint = ps->computeShader.reflection->entryPoint.c_str();
                state.shaders.push_back(std::move(sb));
            }

            // Render Targets (Vulkan: color attachments via renderpass indices)
            {
                const auto& fb = ps->currentPass.framebuffer;
                for (size_t i = 0; i < ps->currentPass.renderpass.colorAttachments.size(); i++) {
                    uint32_t attIdx = ps->currentPass.renderpass.colorAttachments[i];
                    if (attIdx < fb.attachments.size()) {
                        const auto& att = fb.attachments[attIdx];
                        RenderTargetInfo rti;
                        rti.id = toResourceId(att.resource);
                        rti.format = att.format.Name().c_str();
                        fillRTInfo(rti);
                        state.renderTargets.push_back(std::move(rti));
                    }
                }

                // Depth Target
                uint32_t depthIdx = ps->currentPass.renderpass.depthstencilAttachment;
                if (depthIdx < fb.attachments.size()) {
                    const auto& att = fb.attachments[depthIdx];
                    if (att.resource != ::ResourceId::Null()) {
                        RenderTargetInfo dti;
                        dti.id = toResourceId(att.resource);
                        dti.format = att.format.Name().c_str();
                        fillRTInfo(dti);
                        state.depthTarget = std::move(dti);
                    }
                }
            }

            // Viewports
            for (const auto& vps : ps->viewportScissor.viewportScissors) {
                Viewport v;
                v.x = vps.vp.x;
                v.y = vps.vp.y;
                v.width = vps.vp.width;
                v.height = vps.vp.height;
                v.minDepth = vps.vp.minDepth;
                v.maxDepth = vps.vp.maxDepth;
                state.viewports.push_back(v);
            }
            break;
        }

        default:
            break;
    }

    return state;
}

std::map<ShaderStage, StageBindings> getBindings(const Session& session,
                                                   std::optional<uint32_t> eventId) {
    auto* ctrl = session.controller(); // throws NoCaptureOpen if not open

    if (eventId.has_value())
        ctrl->SetFrameEvent(*eventId, true);

    APIProperties props = ctrl->GetAPIProperties();
    std::map<ShaderStage, StageBindings> result;

    switch (props.pipelineType) {
        case GraphicsAPI::D3D11: {
            const auto* state = ctrl->GetD3D11PipelineState();
            if (!state) break;
            if (state->vertexShader.reflection)
                result[ShaderStage::Vertex] = extractStageBindings(state->vertexShader.reflection, state->vertexShader.resourceId);
            if (state->pixelShader.reflection)
                result[ShaderStage::Pixel] = extractStageBindings(state->pixelShader.reflection, state->pixelShader.resourceId);
            if (state->hullShader.reflection)
                result[ShaderStage::Hull] = extractStageBindings(state->hullShader.reflection, state->hullShader.resourceId);
            if (state->domainShader.reflection)
                result[ShaderStage::Domain] = extractStageBindings(state->domainShader.reflection, state->domainShader.resourceId);
            if (state->geometryShader.reflection)
                result[ShaderStage::Geometry] = extractStageBindings(state->geometryShader.reflection, state->geometryShader.resourceId);
            if (state->computeShader.reflection)
                result[ShaderStage::Compute] = extractStageBindings(state->computeShader.reflection, state->computeShader.resourceId);
            break;
        }
        case GraphicsAPI::D3D12: {
            const auto* state = ctrl->GetD3D12PipelineState();
            if (!state) break;
            if (state->vertexShader.reflection)
                result[ShaderStage::Vertex] = extractStageBindings(state->vertexShader.reflection, state->vertexShader.resourceId);
            if (state->pixelShader.reflection)
                result[ShaderStage::Pixel] = extractStageBindings(state->pixelShader.reflection, state->pixelShader.resourceId);
            if (state->hullShader.reflection)
                result[ShaderStage::Hull] = extractStageBindings(state->hullShader.reflection, state->hullShader.resourceId);
            if (state->domainShader.reflection)
                result[ShaderStage::Domain] = extractStageBindings(state->domainShader.reflection, state->domainShader.resourceId);
            if (state->geometryShader.reflection)
                result[ShaderStage::Geometry] = extractStageBindings(state->geometryShader.reflection, state->geometryShader.resourceId);
            if (state->computeShader.reflection)
                result[ShaderStage::Compute] = extractStageBindings(state->computeShader.reflection, state->computeShader.resourceId);
            break;
        }
        case GraphicsAPI::OpenGL: {
            const auto* state = ctrl->GetGLPipelineState();
            if (!state) break;
            // GL uses shaderResourceId
            if (state->vertexShader.reflection)
                result[ShaderStage::Vertex] = extractStageBindings(state->vertexShader.reflection, state->vertexShader.shaderResourceId);
            if (state->fragmentShader.reflection)
                result[ShaderStage::Pixel] = extractStageBindings(state->fragmentShader.reflection, state->fragmentShader.shaderResourceId);
            if (state->tessControlShader.reflection)
                result[ShaderStage::Hull] = extractStageBindings(state->tessControlShader.reflection, state->tessControlShader.shaderResourceId);
            if (state->tessEvalShader.reflection)
                result[ShaderStage::Domain] = extractStageBindings(state->tessEvalShader.reflection, state->tessEvalShader.shaderResourceId);
            if (state->geometryShader.reflection)
                result[ShaderStage::Geometry] = extractStageBindings(state->geometryShader.reflection, state->geometryShader.shaderResourceId);
            if (state->computeShader.reflection)
                result[ShaderStage::Compute] = extractStageBindings(state->computeShader.reflection, state->computeShader.shaderResourceId);
            break;
        }
        case GraphicsAPI::Vulkan: {
            const auto* state = ctrl->GetVulkanPipelineState();
            if (!state) break;
            if (state->vertexShader.reflection)
                result[ShaderStage::Vertex] = extractStageBindings(state->vertexShader.reflection, state->vertexShader.resourceId);
            if (state->fragmentShader.reflection)
                result[ShaderStage::Pixel] = extractStageBindings(state->fragmentShader.reflection, state->fragmentShader.resourceId);
            if (state->tessControlShader.reflection)
                result[ShaderStage::Hull] = extractStageBindings(state->tessControlShader.reflection, state->tessControlShader.resourceId);
            if (state->tessEvalShader.reflection)
                result[ShaderStage::Domain] = extractStageBindings(state->tessEvalShader.reflection, state->tessEvalShader.resourceId);
            if (state->geometryShader.reflection)
                result[ShaderStage::Geometry] = extractStageBindings(state->geometryShader.reflection, state->geometryShader.resourceId);
            if (state->computeShader.reflection)
                result[ShaderStage::Compute] = extractStageBindings(state->computeShader.reflection, state->computeShader.resourceId);
            break;
        }
        default:
            break;
    }

    return result;
}

} // namespace renderdoc::core
