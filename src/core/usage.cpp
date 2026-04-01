#include "core/usage.h"
#include "core/errors.h"
#include "core/session.h"

#include <renderdoc_replay.h>

#include <cstring>

namespace renderdoc::core {

namespace {

// Convert our uint64_t ResourceId alias back to the RenderDoc ::ResourceId struct.
::ResourceId fromResourceId(uint64_t raw) {
    static_assert(sizeof(::ResourceId) == sizeof(uint64_t), "ResourceId size mismatch");
    ::ResourceId id;
    std::memcpy(&id, &raw, sizeof(id));
    return id;
}

// Convert ResourceUsage enum to a human-readable string.
std::string resourceUsageToString(::ResourceUsage usage) {
    switch (usage) {
        case ::ResourceUsage::Unused:            return "Unused";
        case ::ResourceUsage::VertexBuffer:      return "VertexBuffer";
        case ::ResourceUsage::IndexBuffer:       return "IndexBuffer";
        case ::ResourceUsage::VS_Constants:      return "VS_Constants";
        case ::ResourceUsage::HS_Constants:      return "HS_Constants";
        case ::ResourceUsage::DS_Constants:      return "DS_Constants";
        case ::ResourceUsage::GS_Constants:      return "GS_Constants";
        case ::ResourceUsage::PS_Constants:      return "PS_Constants";
        case ::ResourceUsage::CS_Constants:      return "CS_Constants";
        case ::ResourceUsage::TS_Constants:      return "TS_Constants";
        case ::ResourceUsage::MS_Constants:      return "MS_Constants";
        case ::ResourceUsage::All_Constants:     return "All_Constants";
        case ::ResourceUsage::StreamOut:         return "StreamOut";
        case ::ResourceUsage::VS_Resource:       return "VS_Resource";
        case ::ResourceUsage::HS_Resource:       return "HS_Resource";
        case ::ResourceUsage::DS_Resource:       return "DS_Resource";
        case ::ResourceUsage::GS_Resource:       return "GS_Resource";
        case ::ResourceUsage::PS_Resource:       return "PS_Resource";
        case ::ResourceUsage::CS_Resource:       return "CS_Resource";
        case ::ResourceUsage::TS_Resource:       return "TS_Resource";
        case ::ResourceUsage::MS_Resource:       return "MS_Resource";
        case ::ResourceUsage::All_Resource:      return "All_Resource";
        case ::ResourceUsage::VS_RWResource:     return "VS_RWResource";
        case ::ResourceUsage::HS_RWResource:     return "HS_RWResource";
        case ::ResourceUsage::DS_RWResource:     return "DS_RWResource";
        case ::ResourceUsage::GS_RWResource:     return "GS_RWResource";
        case ::ResourceUsage::PS_RWResource:     return "PS_RWResource";
        case ::ResourceUsage::CS_RWResource:     return "CS_RWResource";
        case ::ResourceUsage::TS_RWResource:     return "TS_RWResource";
        case ::ResourceUsage::MS_RWResource:     return "MS_RWResource";
        case ::ResourceUsage::All_RWResource:    return "All_RWResource";
        case ::ResourceUsage::InputTarget:       return "InputTarget";
        case ::ResourceUsage::ColorTarget:       return "ColorTarget";
        case ::ResourceUsage::DepthStencilTarget:return "DepthStencilTarget";
        case ::ResourceUsage::Indirect:          return "Indirect";
        case ::ResourceUsage::Clear:             return "Clear";
        case ::ResourceUsage::Discard:           return "Discard";
        case ::ResourceUsage::GenMips:           return "GenMips";
        case ::ResourceUsage::Resolve:           return "Resolve";
        case ::ResourceUsage::ResolveSrc:        return "ResolveSrc";
        case ::ResourceUsage::ResolveDst:        return "ResolveDst";
        case ::ResourceUsage::Copy:              return "Copy";
        case ::ResourceUsage::CopySrc:           return "CopySrc";
        case ::ResourceUsage::CopyDst:           return "CopyDst";
        case ::ResourceUsage::Barrier:           return "Barrier";
        case ::ResourceUsage::CPUWrite:          return "CPUWrite";
        default:                                 return "Unknown";
    }
}

} // anonymous namespace

ResourceUsageResult getResourceUsage(const Session& session, ResourceId resourceId) {
    auto* ctrl = session.controller();
    if (!ctrl)
        throw CoreError(CoreError::Code::NoCaptureOpen,
                        "No capture is open. Call open_capture first.");

    ::ResourceId rdcId = fromResourceId(resourceId);

    if (rdcId == ::ResourceId::Null())
        throw CoreError(CoreError::Code::InternalError,
                        "Invalid resource ID");

    rdcarray<EventUsage> usages = ctrl->GetUsage(rdcId);

    ResourceUsageResult result;
    result.resourceId = resourceId;
    result.entries.reserve(static_cast<size_t>(usages.count()));

    for (int i = 0; i < usages.count(); i++) {
        ResourceUsageEntry entry;
        entry.eventId = usages[i].eventId;
        entry.usage   = resourceUsageToString(usages[i].usage);
        result.entries.push_back(std::move(entry));
    }

    return result;
}

} // namespace renderdoc::core
