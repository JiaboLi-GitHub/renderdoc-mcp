#include "core/debug.h"
#include "core/constants.h"
#include "core/errors.h"
#include <renderdoc_replay.h>
#include <cstring>

namespace renderdoc::core {

namespace {

static constexpr uint32_t MAX_DEBUG_STEPS = 50000;

std::string varTypeToString(VarType t) {
    switch (t) {
        case VarType::Float:             return "Float";
        case VarType::Double:            return "Double";
        case VarType::Half:              return "Half";
        case VarType::SInt:              return "SInt";
        case VarType::UInt:              return "UInt";
        case VarType::SShort:            return "SShort";
        case VarType::UShort:            return "UShort";
        case VarType::SLong:             return "SLong";
        case VarType::ULong:             return "ULong";
        case VarType::SByte:             return "SByte";
        case VarType::UByte:             return "UByte";
        case VarType::Bool:              return "Bool";
        case VarType::Enum:              return "Enum";
        case VarType::Struct:            return "Struct";
        case VarType::GPUPointer:        return "GPUPointer";
        case VarType::ConstantBlock:     return "ConstantBlock";
        case VarType::ReadOnlyResource:  return "ReadOnlyResource";
        case VarType::ReadWriteResource: return "ReadWriteResource";
        case VarType::Sampler:           return "Sampler";
        default:                         return "Unknown";
    }
}

std::string shaderStageToStr(::ShaderStage s) {
    switch (s) {
        case ::ShaderStage::Vertex:   return "vs";
        case ::ShaderStage::Hull:     return "hs";
        case ::ShaderStage::Domain:   return "ds";
        case ::ShaderStage::Geometry: return "gs";
        case ::ShaderStage::Pixel:    return "ps";
        case ::ShaderStage::Compute:  return "cs";
        default:                      return "unknown";
    }
}

bool isFloatType(VarType t) {
    return t == VarType::Float || t == VarType::Double || t == VarType::Half;
}

bool isSignedIntType(VarType t) {
    return t == VarType::SInt || t == VarType::SShort || t == VarType::SLong || t == VarType::SByte;
}

DebugVariable convertVariable(const ShaderVariable& sv) {
    DebugVariable dv;
    dv.name  = std::string(sv.name.c_str());
    dv.type  = varTypeToString(sv.type);
    dv.rows  = sv.rows;
    dv.cols  = sv.columns;
    dv.flags = (uint32_t)sv.flags;

    uint32_t count = sv.rows * sv.columns;
    if (count == 0) count = 1;

    if (isFloatType(sv.type)) {
        dv.floatValues.resize(count);
        for (uint32_t i = 0; i < count && i < kMaxDebugVarComponents; i++)
            dv.floatValues[i] = sv.value.f32v[i];
    } else if (isSignedIntType(sv.type)) {
        dv.intValues.resize(count);
        for (uint32_t i = 0; i < count && i < kMaxDebugVarComponents; i++)
            dv.intValues[i] = sv.value.s32v[i];
    } else {
        dv.uintValues.resize(count);
        for (uint32_t i = 0; i < count && i < kMaxDebugVarComponents; i++)
            dv.uintValues[i] = sv.value.u32v[i];
    }

    for (size_t i = 0; i < sv.members.size(); i++)
        dv.members.push_back(convertVariable(sv.members[i]));

    return dv;
}

DebugVariableChange convertChange(const ShaderVariableChange& svc) {
    DebugVariableChange dc;
    dc.before = convertVariable(svc.before);
    dc.after  = convertVariable(svc.after);
    return dc;
}

// Recursive helper: find an action by event ID.
const ActionDescription* findActionByEventId(const rdcarray<ActionDescription>& actions,
                                              uint32_t eventId) {
    for (const auto& action : actions) {
        if (action.eventId == eventId)
            return &action;
        if (!action.children.empty()) {
            const ActionDescription* found = findActionByEventId(action.children, eventId);
            if (found)
                return found;
        }
    }
    return nullptr;
}

struct DebugLoopResult {
    uint32_t totalSteps = 0;
    std::vector<DebugVariable> inputs;
    std::vector<DebugVariable> outputs;
    std::vector<DebugStep> trace;
};

DebugLoopResult runDebugLoop(IReplayController* ctrl, ShaderDebugTrace* dbgTrace,
                             bool fullTrace) {
    DebugLoopResult result;
    ShaderDebugger* debugger = dbgTrace->debugger;
    const auto& instInfo = dbgTrace->instInfo;

    // Inputs: use ShaderDebugTrace::inputs (stable, authoritative)
    for (size_t i = 0; i < dbgTrace->inputs.size(); i++)
        result.inputs.push_back(convertVariable(dbgTrace->inputs[i]));

    std::vector<ShaderVariableChange> lastChanges;
    uint32_t stepCount = 0;

    while (stepCount < MAX_DEBUG_STEPS) {
        rdcarray<ShaderDebugState> states = ctrl->ContinueDebug(debugger);
        if (states.empty())
            break;

        for (size_t si = 0; si < states.size() && stepCount < MAX_DEBUG_STEPS; si++) {
            const auto& state = states[si];

            if (!state.changes.empty()) {
                lastChanges.clear();
                for (size_t c = 0; c < state.changes.size(); c++)
                    lastChanges.push_back(state.changes[c]);
            }

            if (fullTrace) {
                DebugStep ds;
                ds.step        = stepCount;
                ds.instruction = state.nextInstruction;

                if (state.nextInstruction < instInfo.size()) {
                    const auto& info = instInfo[state.nextInstruction];
                    ds.line = (int32_t)info.lineInfo.lineStart;
                    if (info.lineInfo.fileIndex >= 0)
                        ds.file = std::to_string(info.lineInfo.fileIndex);
                }

                for (size_t c = 0; c < state.changes.size(); c++)
                    ds.changes.push_back(convertChange(state.changes[c]));

                result.trace.push_back(std::move(ds));
            }

            stepCount++;
        }
    }

    result.totalSteps = stepCount;

    for (const auto& lc : lastChanges)
        result.outputs.push_back(convertVariable(lc.after));

    return result;
}

// Check if a shader stage is marked as not debuggable by RenderDoc and return
// the reason string.  Returns empty string if the shader is debuggable or if
// the check cannot be performed.
std::string getShaderNotDebuggableReason(IReplayController* ctrl, ::ShaderStage stage) {
    APIProperties props = ctrl->GetAPIProperties();
    const ::ShaderReflection* refl = nullptr;

    switch (props.pipelineType) {
        case GraphicsAPI::OpenGL: {
            const auto* s = ctrl->GetGLPipelineState();
            if (!s) break;
            switch (stage) {
                case ::ShaderStage::Vertex:   refl = s->vertexShader.reflection; break;
                case ::ShaderStage::Pixel:    refl = s->fragmentShader.reflection; break;
                case ::ShaderStage::Geometry: refl = s->geometryShader.reflection; break;
                case ::ShaderStage::Hull:     refl = s->tessControlShader.reflection; break;
                case ::ShaderStage::Domain:   refl = s->tessEvalShader.reflection; break;
                case ::ShaderStage::Compute:  refl = s->computeShader.reflection; break;
                default: break;
            }
            break;
        }
        case GraphicsAPI::D3D11: {
            const auto* s = ctrl->GetD3D11PipelineState();
            if (!s) break;
            switch (stage) {
                case ::ShaderStage::Vertex:   refl = s->vertexShader.reflection; break;
                case ::ShaderStage::Pixel:    refl = s->pixelShader.reflection; break;
                case ::ShaderStage::Geometry: refl = s->geometryShader.reflection; break;
                case ::ShaderStage::Hull:     refl = s->hullShader.reflection; break;
                case ::ShaderStage::Domain:   refl = s->domainShader.reflection; break;
                case ::ShaderStage::Compute:  refl = s->computeShader.reflection; break;
                default: break;
            }
            break;
        }
        case GraphicsAPI::D3D12: {
            const auto* s = ctrl->GetD3D12PipelineState();
            if (!s) break;
            switch (stage) {
                case ::ShaderStage::Vertex:   refl = s->vertexShader.reflection; break;
                case ::ShaderStage::Pixel:    refl = s->pixelShader.reflection; break;
                case ::ShaderStage::Geometry: refl = s->geometryShader.reflection; break;
                case ::ShaderStage::Hull:     refl = s->hullShader.reflection; break;
                case ::ShaderStage::Domain:   refl = s->domainShader.reflection; break;
                case ::ShaderStage::Compute:  refl = s->computeShader.reflection; break;
                default: break;
            }
            break;
        }
        case GraphicsAPI::Vulkan: {
            const auto* s = ctrl->GetVulkanPipelineState();
            if (!s) break;
            switch (stage) {
                case ::ShaderStage::Vertex:   refl = s->vertexShader.reflection; break;
                case ::ShaderStage::Pixel:    refl = s->fragmentShader.reflection; break;
                case ::ShaderStage::Geometry: refl = s->geometryShader.reflection; break;
                case ::ShaderStage::Hull:     refl = s->tessControlShader.reflection; break;
                case ::ShaderStage::Domain:   refl = s->tessEvalShader.reflection; break;
                case ::ShaderStage::Compute:  refl = s->computeShader.reflection; break;
                default: break;
            }
            break;
        }
        default: break;
    }

    if (refl && !refl->debugInfo.debuggable)
        return std::string(refl->debugInfo.debugStatus.c_str());

    return {};
}

} // anonymous namespace

ShaderDebugResult debugPixel(
    const Session& session,
    uint32_t eventId,
    uint32_t x, uint32_t y,
    bool fullTrace,
    uint32_t primitive) {

    auto* ctrl = session.controller();
    ctrl->SetFrameEvent(eventId, true);

    DebugPixelInputs inputs;
    inputs.sample    = ~0U;
    inputs.primitive = primitive;
    inputs.view      = ~0U;

    ShaderDebugTrace* trace = ctrl->DebugPixel(x, y, inputs);
    if (!trace || !trace->debugger) {
        if (trace) ctrl->FreeTrace(trace);

        // Distinguish "shader not debuggable" from "no fragment hit" so users
        // know whether the issue is a RenderDoc limitation or a wrong coordinate.
        std::string reason = getShaderNotDebuggableReason(ctrl, ::ShaderStage::Pixel);
        if (!reason.empty())
            throw CoreError(CoreError::Code::DebugNotSupported,
                            "Fragment shader is not debuggable at event " +
                            std::to_string(eventId) + ": " + reason);

        throw CoreError(CoreError::Code::NoFragmentFound,
                        "No fragment hit at (" + std::to_string(x) +
                        "," + std::to_string(y) + ") for event " + std::to_string(eventId) +
                        ". The shader is marked as debuggable, but RenderDoc could "
                        "not produce a debug trace. This can happen with certain "
                        "OpenGL shader features that are not fully supported by "
                        "the software shader debugger.");
    }

    ShaderDebugResult result;
    result.eventId = eventId;
    result.stage   = shaderStageToStr(trace->stage);

    try {
        auto loopResult = runDebugLoop(ctrl, trace, fullTrace);
        result.totalSteps = loopResult.totalSteps;
        result.inputs     = std::move(loopResult.inputs);
        result.outputs    = std::move(loopResult.outputs);
        result.trace      = std::move(loopResult.trace);
    } catch (...) {
        ctrl->FreeTrace(trace);
        throw;
    }

    ctrl->FreeTrace(trace);
    return result;
}

ShaderDebugResult debugVertex(
    const Session& session,
    uint32_t eventId,
    uint32_t vertexId,
    bool fullTrace,
    uint32_t instance,
    uint32_t index,
    uint32_t view) {

    auto* ctrl = session.controller();
    ctrl->SetFrameEvent(eventId, true);

    uint32_t idx = (index == 0xFFFFFFFF) ? vertexId : index;

    ShaderDebugTrace* trace = ctrl->DebugVertex(vertexId, instance, idx, view);
    if (!trace || !trace->debugger) {
        if (trace) ctrl->FreeTrace(trace);

        std::string reason = getShaderNotDebuggableReason(ctrl, ::ShaderStage::Vertex);
        if (!reason.empty())
            throw CoreError(CoreError::Code::DebugNotSupported,
                            "Vertex shader is not debuggable at event " +
                            std::to_string(eventId) + ": " + reason);

        throw CoreError(CoreError::Code::NoFragmentFound,
                        "Cannot debug vertex " + std::to_string(vertexId) +
                        " at event " + std::to_string(eventId) +
                        ". The shader is marked as debuggable, but RenderDoc could "
                        "not produce a debug trace. This can happen with certain "
                        "OpenGL shader features that are not fully supported by "
                        "the software shader debugger.");
    }

    ShaderDebugResult result;
    result.eventId = eventId;
    result.stage   = shaderStageToStr(trace->stage);

    try {
        auto loopResult = runDebugLoop(ctrl, trace, fullTrace);
        result.totalSteps = loopResult.totalSteps;
        result.inputs     = std::move(loopResult.inputs);
        result.outputs    = std::move(loopResult.outputs);
        result.trace      = std::move(loopResult.trace);
    } catch (...) {
        ctrl->FreeTrace(trace);
        throw;
    }

    ctrl->FreeTrace(trace);
    return result;
}

ShaderDebugResult debugThread(
    const Session& session,
    uint32_t eventId,
    uint32_t groupX, uint32_t groupY, uint32_t groupZ,
    uint32_t threadX, uint32_t threadY, uint32_t threadZ,
    bool fullTrace) {

    auto* ctrl = session.controller();
    ctrl->SetFrameEvent(eventId, true);

    const auto& rootActions = ctrl->GetRootActions();
    const ActionDescription* action = findActionByEventId(rootActions, eventId);
    if (!action || !(action->flags & ActionFlags::Dispatch))
        throw CoreError(CoreError::Code::DebugNotSupported,
                        "Event " + std::to_string(eventId) + " is not a dispatch");

    rdcfixedarray<uint32_t, 3> groupid  = {groupX, groupY, groupZ};
    rdcfixedarray<uint32_t, 3> threadid = {threadX, threadY, threadZ};

    ShaderDebugTrace* trace = ctrl->DebugThread(groupid, threadid);
    if (!trace || !trace->debugger) {
        if (trace) ctrl->FreeTrace(trace);

        std::string reason = getShaderNotDebuggableReason(ctrl, ::ShaderStage::Compute);
        if (!reason.empty())
            throw CoreError(CoreError::Code::DebugNotSupported,
                            "Compute shader is not debuggable at event " +
                            std::to_string(eventId) + ": " + reason);

        throw CoreError(CoreError::Code::NoFragmentFound,
                        "Cannot debug thread (" + std::to_string(threadX) + "," +
                        std::to_string(threadY) + "," + std::to_string(threadZ) +
                        ") at event " + std::to_string(eventId));
    }

    ShaderDebugResult result;
    result.eventId = eventId;
    result.stage   = shaderStageToStr(trace->stage);

    try {
        auto loopResult = runDebugLoop(ctrl, trace, fullTrace);
        result.totalSteps = loopResult.totalSteps;
        result.inputs     = std::move(loopResult.inputs);
        result.outputs    = std::move(loopResult.outputs);
        result.trace      = std::move(loopResult.trace);
    } catch (...) {
        ctrl->FreeTrace(trace);
        throw;
    }

    ctrl->FreeTrace(trace);
    return result;
}

} // namespace renderdoc::core
