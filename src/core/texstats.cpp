#include "core/texstats.h"
#include "core/errors.h"
#include <renderdoc_replay.h>
#include <cstring>
#include <algorithm>

namespace renderdoc::core {

namespace {

::ResourceId fromResourceId(ResourceId id) {
    ::ResourceId rid;
    std::memcpy(&rid, &id, sizeof(rid));
    return rid;
}

PixelValue convertPixelValue(const ::PixelValue& pv) {
    PixelValue result;
    for (int i = 0; i < 4; i++) {
        result.floatValue[i] = pv.floatValue[i];
        result.uintValue[i]  = pv.uintValue[i];
        result.intValue[i]   = pv.intValue[i];
    }
    return result;
}

} // anonymous namespace

TextureStats getTextureStats(
    const Session& session,
    ResourceId resourceId,
    uint32_t mip,
    uint32_t slice,
    bool histogram,
    std::optional<uint32_t> eventId) {

    auto* ctrl = session.controller();

    if (eventId.has_value())
        ctrl->SetFrameEvent(*eventId, true);

    ::ResourceId rid = fromResourceId(resourceId);

    // Find texture description by iterating all textures
    auto textures = ctrl->GetTextures();
    TextureDescription tex;
    bool found = false;
    for (int i = 0; i < textures.count(); i++) {
        if (textures[i].resourceId == rid) {
            tex = textures[i];
            found = true;
            break;
        }
    }

    if (!found)
        throw CoreError(CoreError::Code::InvalidResourceId,
                        "Resource not found: " + std::to_string(resourceId));

    if (tex.msSamp > 1)
        throw CoreError(CoreError::Code::InvalidResourceId,
                        "MSAA textures not supported for stats");

    if (mip >= tex.mips)
        throw CoreError(CoreError::Code::InvalidResourceId,
                        "Mip " + std::to_string(mip) + " out of range (0-" +
                        std::to_string(tex.mips - 1) + ")");

    if (slice >= tex.arraysize)
        throw CoreError(CoreError::Code::InvalidResourceId,
                        "Slice " + std::to_string(slice) + " out of range (0-" +
                        std::to_string(tex.arraysize - 1) + ")");

    Subresource sub;
    sub.mip    = mip;
    sub.slice  = slice;
    sub.sample = 0;

    // Determine CompType from texture format for correct union branch access
    CompType compType = CompType::Typeless;
    if (tex.format.compType == CompType::UInt)
        compType = CompType::UInt;
    else if (tex.format.compType == CompType::SInt)
        compType = CompType::SInt;

    auto minmax = ctrl->GetMinMax(rid, sub, compType);

    uint32_t actualEventId = eventId.value_or(session.currentEventId());

    TextureStats result;
    result.id      = resourceId;
    result.eventId = actualEventId;
    result.mip     = mip;
    result.slice   = slice;
    result.minVal  = convertPixelValue(minmax.first);
    result.maxVal  = convertPixelValue(minmax.second);

    if (histogram) {
        result.histogram.resize(256);

        auto getRange = [&](int ch) -> std::pair<float, float> {
            float minF, maxF;
            if (compType == CompType::UInt) {
                minF = static_cast<float>(minmax.first.uintValue[ch]);
                maxF = static_cast<float>(minmax.second.uintValue[ch]);
            } else if (compType == CompType::SInt) {
                minF = static_cast<float>(minmax.first.intValue[ch]);
                maxF = static_cast<float>(minmax.second.intValue[ch]);
            } else {
                minF = minmax.first.floatValue[ch];
                maxF = minmax.second.floatValue[ch];
            }
            if (minF == maxF) maxF = minF + 1.0f;
            return {minF, maxF};
        };

        for (int ch = 0; ch < 4; ch++) {
            rdcfixedarray<bool, 4> channels = {ch == 0, ch == 1, ch == 2, ch == 3};
            auto [minF, maxF] = getRange(ch);

            rdcarray<uint32_t> buckets =
                ctrl->GetHistogram(rid, sub, compType, minF, maxF, channels);

            for (size_t b = 0; b < buckets.size() && b < 256; b++) {
                switch (ch) {
                    case 0: result.histogram[b].r = buckets[b]; break;
                    case 1: result.histogram[b].g = buckets[b]; break;
                    case 2: result.histogram[b].b = buckets[b]; break;
                    case 3: result.histogram[b].a = buckets[b]; break;
                }
            }
        }
    }

    return result;
}

} // namespace renderdoc::core
