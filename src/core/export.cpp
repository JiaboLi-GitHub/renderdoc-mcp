#include "core/export.h"
#include "core/constants.h"
#include "core/errors.h"
#include "core/resource_id.h"
#include "core/session.h"

#include <renderdoc_replay.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace fs = std::filesystem;

namespace renderdoc::core {

namespace {

// Sanitize a string for use in a filename (replace :: with __ and : with _).
std::string sanitizeForFilename(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); i++) {
        if (out[i] == ':' && i + 1 < out.size() && out[i + 1] == ':') {
            out[i]     = '_';
            out[i + 1] = '_';
        } else if (out[i] == ':') {
            out[i] = '_';
        }
    }
    return out;
}

} // namespace

// ── exportRenderTarget ────────────────────────────────────────────────────────

ExportResult exportRenderTarget(const Session& session,
                                int rtIndex,
                                const std::string& outputDir)
{
    auto* ctrl = session.controller();
    if (!ctrl)
        throw CoreError(CoreError::Code::NoCaptureOpen,
                        "No capture is open. Call open_capture first.");

    // Walk the action tree to find the current event and retrieve its output RT.
    const uint32_t eventId = session.currentEventId();
    const auto& actions = ctrl->GetRootActions();

    ::ResourceId rtResourceId;
    bool found = false;

    std::function<bool(const rdcarray<ActionDescription>&)> findAction;
    findAction = [&](const rdcarray<ActionDescription>& acts) -> bool {
        for (const auto& action : acts) {
            if (action.eventId == eventId) {
                if (rtIndex >= 0 && rtIndex < kMaxRenderTargets)
                    rtResourceId = action.outputs[rtIndex];
                found = true;
                return true;
            }
            if (!action.children.empty() && findAction(action.children))
                return true;
        }
        return false;
    };
    findAction(actions);

    if (!found)
        throw CoreError(CoreError::Code::InvalidEventId,
                        "Current event not found in action list. Call goto_event first.");

    if (rtResourceId == ::ResourceId::Null())
        throw CoreError(CoreError::Code::ExportFailed,
                        "No render target bound at index " + std::to_string(rtIndex) +
                        " for event " + std::to_string(eventId));

    // Build output path.
    fs::create_directories(outputDir);
    std::ostringstream filename;
    filename << "rt_" << eventId << "_" << rtIndex << ".png";
    std::string outputPath = (fs::path(outputDir) / filename.str()).string();

    // Save as PNG.
    TextureSave saveData = {};
    saveData.resourceId = rtResourceId;
    saveData.destType   = FileType::PNG;
    saveData.alpha      = AlphaMapping::Preserve;

    ResultDetails res = ctrl->SaveTexture(saveData, rdcstr(outputPath.c_str()));
    if (!res.OK())
        throw CoreError(CoreError::Code::ExportFailed,
                        "Failed to save render target: " + std::string(res.Message().c_str()));

    // Retrieve texture dimensions.
    const auto& textures = ctrl->GetTextures();
    uint32_t width = 0, height = 0;
    for (const auto& tex : textures) {
        if (tex.resourceId == rtResourceId) {
            width  = tex.width;
            height = tex.height;
            break;
        }
    }

    ExportResult result;
    result.outputPath  = outputPath;
    result.eventId     = eventId;
    result.rtIndex     = rtIndex;
    result.width       = width;
    result.height      = height;
    result.resourceId  = toResourceId(rtResourceId);
    return result;
}

// ── exportTexture ─────────────────────────────────────────────────────────────

ExportResult exportTexture(const Session& session,
                           ResourceId id,
                           const std::string& outputDir,
                           uint32_t mip,
                           uint32_t layer)
{
    auto* ctrl = session.controller();
    if (!ctrl)
        throw CoreError(CoreError::Code::NoCaptureOpen,
                        "No capture is open. Call open_capture first.");

    ::ResourceId rdcId = fromResourceId(id);

    // Build a safe filename from the resource ID.
    std::string idStr = "ResourceId__" + std::to_string(id);
    fs::create_directories(outputDir);
    std::string filename = "tex_" + idStr + "_mip" + std::to_string(mip) + ".png";
    std::string outputPath = (fs::path(outputDir) / filename).string();

    TextureSave saveData = {};
    saveData.resourceId          = rdcId;
    saveData.mip                 = static_cast<int32_t>(mip);
    saveData.slice.sliceIndex    = static_cast<int32_t>(layer);
    saveData.destType            = FileType::PNG;
    saveData.alpha               = AlphaMapping::Preserve;

    ResultDetails res = ctrl->SaveTexture(saveData, rdcstr(outputPath.c_str()));
    if (!res.OK())
        throw CoreError(CoreError::Code::ExportFailed,
                        "Failed to save texture: " + std::string(res.Message().c_str()));

    ExportResult result;
    result.outputPath = outputPath;
    result.resourceId = id;
    result.mip        = mip;
    result.layer      = layer;
    return result;
}

// ── exportBuffer ──────────────────────────────────────────────────────────────

ExportResult exportBuffer(const Session& session,
                          ResourceId id,
                          const std::string& outputDir,
                          uint64_t offset,
                          uint64_t size)
{
    auto* ctrl = session.controller();
    if (!ctrl)
        throw CoreError(CoreError::Code::NoCaptureOpen,
                        "No capture is open. Call open_capture first.");

    ::ResourceId rdcId = fromResourceId(id);
    rdcarray<byte> data = ctrl->GetBufferData(rdcId, offset, size);

    // Build a safe filename from the resource ID.
    std::string idStr = "ResourceId__" + std::to_string(id);
    fs::create_directories(outputDir);
    std::string outputPath = (fs::path(outputDir) / ("buf_" + idStr + ".bin")).string();

    std::ofstream ofs(outputPath, std::ios::binary);
    if (!ofs)
        throw CoreError(CoreError::Code::ExportFailed,
                        "Failed to open output file: " + outputPath);
    ofs.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    ofs.close();

    ExportResult result;
    result.outputPath    = outputPath;
    result.byteSize      = static_cast<uint64_t>(data.size());
    result.resourceId    = id;
    result.offset        = offset;
    result.requestedSize = size;
    return result;
}

} // namespace renderdoc::core
