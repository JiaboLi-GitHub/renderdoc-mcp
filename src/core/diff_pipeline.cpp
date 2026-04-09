#include "core/diff_internal.h"
#include "core/assertions.h"
#include "core/constants.h"
#include "core/errors.h"
#include "core/pipeline.h"

#include <filesystem>
#include <functional>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace renderdoc::core {

using namespace diff_internal;

// ---------------------------------------------------------------------------
// diffPipeline
// ---------------------------------------------------------------------------
PipelineDiffResult diffPipeline(DiffSession& session, const std::string& markerPath)
{
    IReplayController* ctrlA = session.controllerA();  // throws if not open
    IReplayController* ctrlB = session.controllerB();  // throws if not open

    // Parse optional [N] index suffix from markerPath
    std::string path = markerPath;
    int nthMatch = 0;
    {
        auto bracketPos = path.rfind('[');
        if (bracketPos != std::string::npos && path.back() == ']') {
            std::string idxStr = path.substr(bracketPos + 1, path.size() - bracketPos - 2);
            try {
                nthMatch = std::stoi(idxStr);
            } catch (...) {
                nthMatch = 0;
            }
            path = path.substr(0, bracketPos);
        }
    }

    // Get draw alignment
    DrawsDiffResult draws = diffDraws(session);

    // Find the Nth matched pair at the given markerPath
    uint32_t eidA = 0, eidB = 0;
    int matchCount = 0;
    for (const auto& row : draws.rows) {
        if (row.status == DiffStatus::Equal && row.a.has_value() && row.b.has_value()) {
            bool pathMatch = path.empty() ||
                             toLower(row.a->markerPath).find(toLower(path)) != std::string::npos;
            if (pathMatch) {
                if (matchCount == nthMatch) {
                    eidA = row.a->eventId;
                    eidB = row.b->eventId;
                    break;
                }
                matchCount++;
            }
        }
    }

    if (eidA == 0 || eidB == 0)
        throw CoreError(CoreError::Code::InvalidEventId,
                        "No matched draw found for markerPath: " + markerPath);

    // Navigate both controllers
    ctrlA->SetFrameEvent(eidA, true);
    ctrlB->SetFrameEvent(eidB, true);

    // Get pipeline state from both controllers.
    // We use a helper lambda to extract state directly from the controller
    // (similar to getPipelineState but for IReplayController directly).
    auto extractState = [](IReplayController* ctrl) -> PipelineState {
        APIProperties props = ctrl->GetAPIProperties();
        PipelineState state;

        switch (props.pipelineType) {
            case GraphicsAPI::D3D11: {
                state.api = GraphicsApi::D3D11;
                const D3D11Pipe::State* ps = ctrl->GetD3D11PipelineState();
                if (!ps) break;
                auto addShader = [&](ShaderStage stage, ::ResourceId rid, const ::ShaderReflection* refl) {
                    PipelineState::ShaderBinding sb;
                    sb.stage = stage;
                    sb.shaderId = toResourceId(rid);
                    if (refl) sb.entryPoint = refl->entryPoint.c_str();
                    state.shaders.push_back(std::move(sb));
                };
                addShader(ShaderStage::Vertex, ps->vertexShader.resourceId, ps->vertexShader.reflection);
                addShader(ShaderStage::Pixel, ps->pixelShader.resourceId, ps->pixelShader.reflection);
                for (const auto& rt : ps->outputMerger.renderTargets) {
                    if (rt.resource == ::ResourceId::Null()) continue;
                    RenderTargetInfo rti;
                    rti.id = toResourceId(rt.resource);
                    rti.format = rt.format.Name().c_str();
                    state.renderTargets.push_back(std::move(rti));
                }
                if (ps->outputMerger.depthTarget.resource != ::ResourceId::Null()) {
                    RenderTargetInfo dti;
                    dti.id = toResourceId(ps->outputMerger.depthTarget.resource);
                    dti.format = ps->outputMerger.depthTarget.format.Name().c_str();
                    state.depthTarget = std::move(dti);
                }
                for (const auto& vp : ps->rasterizer.viewports) {
                    if (!vp.enabled) continue;
                    Viewport v{vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth};
                    state.viewports.push_back(v);
                }
                break;
            }
            case GraphicsAPI::D3D12: {
                state.api = GraphicsApi::D3D12;
                const D3D12Pipe::State* ps = ctrl->GetD3D12PipelineState();
                if (!ps) break;
                auto addShader = [&](ShaderStage stage, ::ResourceId rid, const ::ShaderReflection* refl) {
                    PipelineState::ShaderBinding sb;
                    sb.stage = stage;
                    sb.shaderId = toResourceId(rid);
                    if (refl) sb.entryPoint = refl->entryPoint.c_str();
                    state.shaders.push_back(std::move(sb));
                };
                addShader(ShaderStage::Vertex, ps->vertexShader.resourceId, ps->vertexShader.reflection);
                addShader(ShaderStage::Pixel, ps->pixelShader.resourceId, ps->pixelShader.reflection);
                for (const auto& rt : ps->outputMerger.renderTargets) {
                    if (rt.resource == ::ResourceId::Null()) continue;
                    RenderTargetInfo rti;
                    rti.id = toResourceId(rt.resource);
                    rti.format = rt.format.Name().c_str();
                    state.renderTargets.push_back(std::move(rti));
                }
                if (ps->outputMerger.depthTarget.resource != ::ResourceId::Null()) {
                    RenderTargetInfo dti;
                    dti.id = toResourceId(ps->outputMerger.depthTarget.resource);
                    dti.format = ps->outputMerger.depthTarget.format.Name().c_str();
                    state.depthTarget = std::move(dti);
                }
                for (const auto& vp : ps->rasterizer.viewports) {
                    if (!vp.enabled) continue;
                    Viewport v{vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth};
                    state.viewports.push_back(v);
                }
                break;
            }
            case GraphicsAPI::OpenGL: {
                state.api = GraphicsApi::OpenGL;
                const GLPipe::State* ps = ctrl->GetGLPipelineState();
                if (!ps) break;
                auto addShader = [&](ShaderStage stage, ::ResourceId rid, const ::ShaderReflection* refl) {
                    PipelineState::ShaderBinding sb;
                    sb.stage = stage;
                    sb.shaderId = toResourceId(rid);
                    if (refl) sb.entryPoint = refl->entryPoint.c_str();
                    state.shaders.push_back(std::move(sb));
                };
                addShader(ShaderStage::Vertex, ps->vertexShader.shaderResourceId, ps->vertexShader.reflection);
                addShader(ShaderStage::Pixel, ps->fragmentShader.shaderResourceId, ps->fragmentShader.reflection);
                for (const auto& att : ps->framebuffer.drawFBO.colorAttachments) {
                    if (att.resource == ::ResourceId::Null()) continue;
                    RenderTargetInfo rti;
                    rti.id = toResourceId(att.resource);
                    state.renderTargets.push_back(std::move(rti));
                }
                if (ps->framebuffer.drawFBO.depthAttachment.resource != ::ResourceId::Null()) {
                    RenderTargetInfo dti;
                    dti.id = toResourceId(ps->framebuffer.drawFBO.depthAttachment.resource);
                    state.depthTarget = std::move(dti);
                }
                for (const auto& vp : ps->rasterizer.viewports) {
                    if (!vp.enabled) continue;
                    Viewport v{vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth};
                    state.viewports.push_back(v);
                }
                break;
            }
            case GraphicsAPI::Vulkan: {
                state.api = GraphicsApi::Vulkan;
                const VKPipe::State* ps = ctrl->GetVulkanPipelineState();
                if (!ps) break;
                auto addShader = [&](ShaderStage stage, ::ResourceId rid, const ::ShaderReflection* refl) {
                    PipelineState::ShaderBinding sb;
                    sb.stage = stage;
                    sb.shaderId = toResourceId(rid);
                    if (refl) sb.entryPoint = refl->entryPoint.c_str();
                    state.shaders.push_back(std::move(sb));
                };
                addShader(ShaderStage::Vertex, ps->vertexShader.resourceId, ps->vertexShader.reflection);
                addShader(ShaderStage::Pixel, ps->fragmentShader.resourceId, ps->fragmentShader.reflection);
                const auto& fb = ps->currentPass.framebuffer;
                for (uint32_t attIdx : ps->currentPass.renderpass.colorAttachments) {
                    if (attIdx < (uint32_t)fb.attachments.size()) {
                        const auto& att = fb.attachments[attIdx];
                        RenderTargetInfo rti;
                        rti.id = toResourceId(att.resource);
                        rti.format = att.format.Name().c_str();
                        state.renderTargets.push_back(std::move(rti));
                    }
                }
                uint32_t depthIdx = ps->currentPass.renderpass.depthstencilAttachment;
                if (depthIdx < (uint32_t)fb.attachments.size()) {
                    const auto& att = fb.attachments[depthIdx];
                    if (att.resource != ::ResourceId::Null()) {
                        RenderTargetInfo dti;
                        dti.id = toResourceId(att.resource);
                        dti.format = att.format.Name().c_str();
                        state.depthTarget = std::move(dti);
                    }
                }
                for (const auto& vps : ps->viewportScissor.viewportScissors) {
                    Viewport v{vps.vp.x, vps.vp.y, vps.vp.width, vps.vp.height,
                               vps.vp.minDepth, vps.vp.maxDepth};
                    state.viewports.push_back(v);
                }
                break;
            }
            default:
                break;
        }
        return state;
    };

    PipelineState stateA = extractState(ctrlA);
    PipelineState stateB = extractState(ctrlB);

    PipelineDiffResult result;
    result.eidA = eidA;
    result.eidB = eidB;
    result.markerPath = markerPath;

    auto addField = [&](const std::string& section, const std::string& field,
                        const std::string& va, const std::string& vb) {
        PipeFieldDiff f;
        f.section = section;
        f.field = field;
        f.valueA = va;
        f.valueB = vb;
        f.changed = (va != vb);
        if (f.changed) result.changedCount++;
        result.totalCount++;
        result.fields.push_back(std::move(f));
    };

    // Compare shader bindings
    size_t numShaders = std::max(stateA.shaders.size(), stateB.shaders.size());
    for (size_t i = 0; i < numShaders; ++i) {
        std::string section = "Shader[" + std::to_string(i) + "]";
        std::string stageA = (i < stateA.shaders.size()) ? stageName(stateA.shaders[i].stage) : "";
        std::string stageB = (i < stateB.shaders.size()) ? stageName(stateB.shaders[i].stage) : "";
        addField(section, "stage", stageA, stageB);

        std::string idA = (i < stateA.shaders.size()) ? std::to_string(stateA.shaders[i].shaderId) : "";
        std::string idB = (i < stateB.shaders.size()) ? std::to_string(stateB.shaders[i].shaderId) : "";
        addField(section, "shaderId", idA, idB);

        std::string epA = (i < stateA.shaders.size()) ? stateA.shaders[i].entryPoint : "";
        std::string epB = (i < stateB.shaders.size()) ? stateB.shaders[i].entryPoint : "";
        addField(section, "entryPoint", epA, epB);
    }

    // Compare render targets
    size_t numRTs = std::max(stateA.renderTargets.size(), stateB.renderTargets.size());
    for (size_t i = 0; i < numRTs; ++i) {
        std::string section = "RT[" + std::to_string(i) + "]";
        std::string idA = (i < stateA.renderTargets.size()) ? std::to_string(stateA.renderTargets[i].id) : "";
        std::string idB = (i < stateB.renderTargets.size()) ? std::to_string(stateB.renderTargets[i].id) : "";
        addField(section, "id", idA, idB);

        std::string nameA = (i < stateA.renderTargets.size()) ? stateA.renderTargets[i].name : "";
        std::string nameB = (i < stateB.renderTargets.size()) ? stateB.renderTargets[i].name : "";
        addField(section, "name", nameA, nameB);

        std::string wA = (i < stateA.renderTargets.size()) ? std::to_string(stateA.renderTargets[i].width) : "";
        std::string wB = (i < stateB.renderTargets.size()) ? std::to_string(stateB.renderTargets[i].width) : "";
        addField(section, "width", wA, wB);

        std::string hA = (i < stateA.renderTargets.size()) ? std::to_string(stateA.renderTargets[i].height) : "";
        std::string hB = (i < stateB.renderTargets.size()) ? std::to_string(stateB.renderTargets[i].height) : "";
        addField(section, "height", hA, hB);

        std::string fmtA = (i < stateA.renderTargets.size()) ? stateA.renderTargets[i].format : "";
        std::string fmtB = (i < stateB.renderTargets.size()) ? stateB.renderTargets[i].format : "";
        addField(section, "format", fmtA, fmtB);
    }

    // Compare depth target
    {
        std::string idA = stateA.depthTarget ? std::to_string(stateA.depthTarget->id) : "";
        std::string idB = stateB.depthTarget ? std::to_string(stateB.depthTarget->id) : "";
        addField("Depth", "id", idA, idB);

        std::string fmtA = stateA.depthTarget ? stateA.depthTarget->format : "";
        std::string fmtB = stateB.depthTarget ? stateB.depthTarget->format : "";
        addField("Depth", "format", fmtA, fmtB);
    }

    // Compare viewports
    size_t numVPs = std::max(stateA.viewports.size(), stateB.viewports.size());
    for (size_t i = 0; i < numVPs; ++i) {
        std::string section = "VP[" + std::to_string(i) + "]";
        auto vpStr = [](const Viewport& v) -> std::string {
            std::ostringstream ss;
            ss << v.x << "," << v.y << "," << v.width << "," << v.height;
            return ss.str();
        };
        std::string vA = (i < stateA.viewports.size()) ? vpStr(stateA.viewports[i]) : "";
        std::string vB = (i < stateB.viewports.size()) ? vpStr(stateB.viewports[i]) : "";
        addField(section, "rect", vA, vB);
    }

    return result;
}

// ---------------------------------------------------------------------------
// diffFramebuffer
// ---------------------------------------------------------------------------
ImageCompareResult diffFramebuffer(DiffSession& session,
                                    uint32_t eidA, uint32_t eidB,
                                    int target,
                                    double threshold,
                                    const std::string& diffOutput)
{
    IReplayController* ctrlA = session.controllerA();
    IReplayController* ctrlB = session.controllerB();
    if (!ctrlA || !ctrlB)
        throw CoreError(CoreError::Code::NoCaptureOpen, "DiffSession not open.");

    // If eidA == 0, find last draw in capture A
    if (eidA == 0) {
        const auto& rootA = ctrlA->GetRootActions();
        const ActionDescription* last = findLastDraw(rootA);
        if (last) eidA = last->eventId;
        else throw CoreError(CoreError::Code::InvalidEventId, "No draw found in capture A.");
    }
    if (eidB == 0) {
        const auto& rootB = ctrlB->GetRootActions();
        const ActionDescription* last = findLastDraw(rootB);
        if (last) eidB = last->eventId;
        else throw CoreError(CoreError::Code::InvalidEventId, "No draw found in capture B.");
    }

    // Navigate both controllers
    ctrlA->SetFrameEvent(eidA, true);
    ctrlB->SetFrameEvent(eidB, true);

    // Find the RT resource ID for each event
    auto findRtResourceId = [](IReplayController* ctrl, uint32_t eid, int rtIdx) -> ::ResourceId {
        const auto& actions = ctrl->GetRootActions();
        std::function<const ActionDescription*(const rdcarray<ActionDescription>&)> find;
        find = [&](const rdcarray<ActionDescription>& acts) -> const ActionDescription* {
            for (const auto& a : acts) {
                if (a.eventId == eid) return &a;
                if (!a.children.empty()) {
                    const ActionDescription* f = find(a.children);
                    if (f) return f;
                }
            }
            return nullptr;
        };
        const ActionDescription* action = find(actions);
        if (!action) return ::ResourceId::Null();
        if (rtIdx >= 0 && rtIdx < kMaxRenderTargets) return action->outputs[rtIdx];
        return ::ResourceId::Null();
    };

    ::ResourceId rtA = findRtResourceId(ctrlA, eidA, target);
    ::ResourceId rtB = findRtResourceId(ctrlB, eidB, target);

    if (rtA == ::ResourceId::Null())
        throw CoreError(CoreError::Code::ExportFailed,
                        "No RT at index " + std::to_string(target) + " for event " + std::to_string(eidA));
    if (rtB == ::ResourceId::Null())
        throw CoreError(CoreError::Code::ExportFailed,
                        "No RT at index " + std::to_string(target) + " for event " + std::to_string(eidB));

    // Create temp directory for PNGs
    fs::path tmpDir = fs::temp_directory_path() / "renderdoc_diff";
    fs::create_directories(tmpDir);

    std::string pathA = (tmpDir / ("diff_A_" + std::to_string(eidA) + "_rt" + std::to_string(target) + ".png")).string();
    std::string pathB = (tmpDir / ("diff_B_" + std::to_string(eidB) + "_rt" + std::to_string(target) + ".png")).string();

    // Save A
    {
        TextureSave saveData = {};
        saveData.resourceId = rtA;
        saveData.destType   = FileType::PNG;
        saveData.alpha      = AlphaMapping::Preserve;
        ResultDetails res = ctrlA->SaveTexture(saveData, rdcstr(pathA.c_str()));
        if (!res.OK())
            throw CoreError(CoreError::Code::ExportFailed,
                            "Failed to save RT A: " + std::string(res.Message().c_str()));
    }

    // Save B
    {
        TextureSave saveData = {};
        saveData.resourceId = rtB;
        saveData.destType   = FileType::PNG;
        saveData.alpha      = AlphaMapping::Preserve;
        ResultDetails res = ctrlB->SaveTexture(saveData, rdcstr(pathB.c_str()));
        if (!res.OK())
            throw CoreError(CoreError::Code::ExportFailed,
                            "Failed to save RT B: " + std::string(res.Message().c_str()));
    }

    // Compare using assertImage
    return assertImage(pathA, pathB, threshold, diffOutput);
}

} // namespace renderdoc::core
