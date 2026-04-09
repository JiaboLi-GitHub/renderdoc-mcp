#include "core/pixel.h"
#include "core/constants.h"
#include "core/errors.h"
#include "core/resource_id.h"
#include <renderdoc_replay.h>
#include <cmath>
#include <algorithm>

namespace renderdoc::core {

namespace {

PixelValue convertPixelValue(const ::PixelValue& pv) {
    PixelValue result;
    for (int i = 0; i < 4; i++) {
        result.floatValue[i] = pv.floatValue[i];
        result.uintValue[i]  = pv.uintValue[i];
        result.intValue[i]   = pv.intValue[i];
    }
    return result;
}

std::vector<std::string> collectFlags(const ::PixelModification& mod) {
    std::vector<std::string> flags;
    if (mod.directShaderWrite) flags.push_back("directShaderWrite");
    if (mod.unboundPS)         flags.push_back("unboundPS");
    if (mod.sampleMasked)      flags.push_back("sampleMasked");
    if (mod.backfaceCulled)    flags.push_back("backfaceCulled");
    if (mod.depthClipped)      flags.push_back("depthClipped");
    if (mod.depthBoundsFailed) flags.push_back("depthBoundsFailed");
    if (mod.viewClipped)       flags.push_back("viewClipped");
    if (mod.scissorClipped)    flags.push_back("scissorClipped");
    if (mod.shaderDiscarded)   flags.push_back("shaderDiscarded");
    if (mod.depthTestFailed)   flags.push_back("depthTestFailed");
    if (mod.stencilTestFailed) flags.push_back("stencilTestFailed");
    if (mod.predicationSkipped) flags.push_back("predicationSkipped");
    return flags;
}

std::optional<float> safeDepth(float d) {
    if (d == -1.0f || !std::isfinite(d))
        return std::nullopt;
    return d;
}

struct TargetInfo {
    ::ResourceId rid;
    uint32_t width;
    uint32_t height;
    uint32_t firstMip;
    uint32_t firstSlice;
};

// Resolve the render target resource ID from the action tree (avoids PipeState
// virtual methods that are not exported from renderdoc.dll).
TargetInfo resolveTarget(IReplayController* ctrl, uint32_t targetIndex,
                         uint32_t eventId, uint32_t x, uint32_t y) {
    if (targetIndex >= kMaxRenderTargets)
        throw CoreError(CoreError::Code::TargetNotFound,
                        "Target index " + std::to_string(targetIndex) +
                        " out of range (0-" + std::to_string(kMaxRenderTargets - 1) + ")");

    const rdcarray<ActionDescription>& roots = ctrl->GetRootActions();
    ::ResourceId rtId;
    bool found = false;

    std::function<bool(const rdcarray<ActionDescription>&)> findAction;
    findAction = [&](const rdcarray<ActionDescription>& acts) -> bool {
        for (const auto& act : acts) {
            if (act.eventId == eventId) {
                rtId = act.outputs[targetIndex];
                found = true;
                return true;
            }
            if (!act.children.empty() && findAction(act.children))
                return true;
        }
        return false;
    };
    findAction(roots);

    if (!found)
        throw CoreError(CoreError::Code::InvalidEventId,
                        "Event " + std::to_string(eventId) +
                        " not found in action list");

    if (rtId == ::ResourceId::Null())
        throw CoreError(CoreError::Code::TargetNotFound,
                        "Target index " + std::to_string(targetIndex) +
                        " is null at event " + std::to_string(eventId));

    // Find texture description in texture list.
    const rdcarray<TextureDescription>& textures = ctrl->GetTextures();
    TextureDescription tex = {};
    bool texFound = false;
    for (int i = 0; i < textures.count(); i++) {
        if (textures[i].resourceId == rtId) {
            tex = textures[i];
            texFound = true;
            break;
        }
    }
    if (!texFound)
        throw CoreError(CoreError::Code::TargetNotFound,
                        "Texture not found for target index " + std::to_string(targetIndex));

    if (tex.msSamp > 1)
        throw CoreError(CoreError::Code::TargetNotFound,
                        "MSAA targets not supported in Phase 1");

    // Render targets are always bound at mip 0 / slice 0 in standard usage.
    uint32_t mipWidth  = std::max(1u, tex.width);
    uint32_t mipHeight = std::max(1u, tex.height);

    if (x >= mipWidth || y >= mipHeight)
        throw CoreError(CoreError::Code::InvalidCoordinates,
                        "(" + std::to_string(x) + "," + std::to_string(y) +
                        ") out of bounds for target " +
                        std::to_string(mipWidth) + "x" + std::to_string(mipHeight));

    return {rtId, mipWidth, mipHeight, 0u, 0u};
}

} // anonymous namespace

PixelHistoryResult pixelHistory(
    const Session& session,
    uint32_t x, uint32_t y,
    uint32_t targetIndex,
    std::optional<uint32_t> eventId) {

    auto* ctrl = session.controller();
    uint32_t actualEventId = eventId.value_or(session.currentEventId());
    if (eventId.has_value())
        ctrl->SetFrameEvent(*eventId, true);

    auto target = resolveTarget(ctrl, targetIndex, actualEventId, x, y);

    Subresource sub;
    sub.mip    = target.firstMip;
    sub.slice  = target.firstSlice;
    sub.sample = 0;

    rdcarray<::PixelModification> history =
        ctrl->PixelHistory(target.rid, x, y, sub, CompType::Typeless);

    PixelHistoryResult result;
    result.x = x;
    result.y = y;
    result.eventId = actualEventId;
    result.targetIndex = targetIndex;
    result.targetId = toResourceId(target.rid);

    for (int i = 0; i < history.count(); i++) {
        const auto& mod = history[i];
        PixelModification pm;
        pm.eventId       = mod.eventId;
        pm.fragmentIndex = mod.fragIndex;
        pm.primitiveId   = mod.primitiveID;
        pm.shaderOut     = convertPixelValue(mod.shaderOut.col);
        pm.postMod       = convertPixelValue(mod.postMod.col);
        pm.depth         = safeDepth(mod.postMod.depth);
        pm.passed        = mod.Passed();
        pm.flags         = collectFlags(mod);
        result.modifications.push_back(std::move(pm));
    }

    return result;
}

PickPixelResult pickPixel(
    const Session& session,
    uint32_t x, uint32_t y,
    uint32_t targetIndex,
    std::optional<uint32_t> eventId) {

    auto* ctrl = session.controller();
    uint32_t actualEventId = eventId.value_or(session.currentEventId());
    if (eventId.has_value())
        ctrl->SetFrameEvent(*eventId, true);

    auto target = resolveTarget(ctrl, targetIndex, actualEventId, x, y);

    Subresource sub;
    sub.mip    = target.firstMip;
    sub.slice  = target.firstSlice;
    sub.sample = 0;

    ::PixelValue pv = ctrl->PickPixel(target.rid, x, y, sub, CompType::Typeless);

    PickPixelResult result;
    result.x = x;
    result.y = y;
    result.eventId = actualEventId;
    result.targetIndex = targetIndex;
    result.targetId = toResourceId(target.rid);
    result.color = convertPixelValue(pv);

    return result;
}

} // namespace renderdoc::core
