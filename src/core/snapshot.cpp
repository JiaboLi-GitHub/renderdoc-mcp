#include "core/snapshot.h"
#include "core/constants.h"
#include "core/errors.h"
#include "core/export.h"
#include "core/pipeline.h"
#include "core/resource_id.h"
#include "core/session.h"
#include "core/shaders.h"

#include <renderdoc_replay.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace fs = std::filesystem;

namespace renderdoc::core {

namespace {

// Map ShaderStage to a short string for filenames.
std::string stageToFileSuffix(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:   return "vs";
        case ShaderStage::Hull:     return "hs";
        case ShaderStage::Domain:   return "ds";
        case ShaderStage::Geometry: return "gs";
        case ShaderStage::Pixel:    return "ps";
        case ShaderStage::Compute:  return "cs";
    }
    return "unknown";
}

} // anonymous namespace

SnapshotResult exportSnapshot(Session& session, uint32_t eventId,
                              const std::string& outputDir,
                              std::function<std::string(const PipelineState&)> pipelineSerializer) {
    // Validate output directory for path traversal
    auto normalizedDir = fs::path(outputDir).lexically_normal().string();
    if (normalizedDir.find("..") != std::string::npos)
        throw CoreError(CoreError::Code::InvalidPath,
                        "Output directory must not contain path traversal (..): " + outputDir);

    auto* ctrl = session.controller();

    ctrl->SetFrameEvent(eventId, true);

    fs::create_directories(outputDir);

    SnapshotResult result;

    // 1. Export pipeline state via injected serializer.
    try {
        PipelineState pipeState = getPipelineState(session, eventId);
        std::string pipeJson = pipelineSerializer(pipeState);
        std::string pipePath = (fs::path(outputDir) / "pipeline.json").string();
        std::ofstream ofs(pipePath);
        if (ofs) {
            ofs << pipeJson;
            ofs.close();
            result.files.push_back(pipePath);
        } else {
            result.errors.push_back("Failed to write pipeline.json");
        }
    } catch (const std::exception& e) {
        result.errors.push_back(std::string("Pipeline export error: ") + e.what());
    }

    // 2. Export shader disassembly for each active stage.
    static const ShaderStage allStages[] = {
        ShaderStage::Vertex, ShaderStage::Hull, ShaderStage::Domain,
        ShaderStage::Geometry, ShaderStage::Pixel, ShaderStage::Compute
    };

    for (ShaderStage stage : allStages) {
        try {
            ShaderDisassembly disasm = getShaderDisassembly(session, stage, eventId);
            if (!disasm.disassembly.empty()) {
                std::string filename = "shader_" + stageToFileSuffix(stage) + ".txt";
                std::string path = (fs::path(outputDir) / filename).string();
                std::ofstream ofs(path);
                if (ofs) {
                    ofs << disasm.disassembly;
                    ofs.close();
                    result.files.push_back(path);
                }
            }
        } catch (const CoreError& e) {
            // No shader bound at this stage — skip silently for expected errors.
            if (e.code() != CoreError::Code::NoShaderBound)
                result.errors.push_back(std::string("Shader export (") + stageToFileSuffix(stage) + "): " + e.what());
        } catch (const std::exception& e) {
            result.errors.push_back(std::string("Shader export (") + stageToFileSuffix(stage) + "): " + e.what());
        }
    }

    // 3. Export render targets (color 0-7).
    for (int i = 0; i < kMaxRenderTargets; i++) {
        try {
            ExportResult exp = exportRenderTarget(session, i, outputDir);
            if (!exp.outputPath.empty()) {
                // Rename to color{i}.png for snapshot convention.
                std::string targetName = "color" + std::to_string(i) + ".png";
                std::string targetPath = (fs::path(outputDir) / targetName).string();
                try {
                    fs::rename(exp.outputPath, targetPath);
                    result.files.push_back(targetPath);
                } catch (const std::exception&) {
                    // If rename fails, keep original path.
                    result.files.push_back(exp.outputPath);
                }
            }
        } catch (const CoreError& e) {
            // No RT at this index — skip silently for expected errors.
            if (e.code() != CoreError::Code::ExportFailed && e.code() != CoreError::Code::InvalidEventId)
                result.errors.push_back(std::string("RT export (") + std::to_string(i) + "): " + e.what());
        } catch (const std::exception& e) {
            result.errors.push_back(std::string("RT export (") + std::to_string(i) + "): " + e.what());
        }
    }

    // 4. Export depth target.
    {
        // Walk action tree to find depth target for this event.
        const rdcarray<ActionDescription>& roots = ctrl->GetRootActions();
        ::ResourceId depthId;
        bool found = false;

        std::function<bool(const rdcarray<ActionDescription>&)> findAction;
        findAction = [&](const rdcarray<ActionDescription>& acts) -> bool {
            for (const auto& act : acts) {
                if (act.eventId == eventId) {
                    depthId = act.depthOut;
                    found = true;
                    return true;
                }
                if (!act.children.empty() && findAction(act.children))
                    return true;
            }
            return false;
        };
        findAction(roots);

        if (found && depthId != ::ResourceId::Null()) {
            try {
                // Convert to our ResourceId for exportTexture.
                uint64_t rawDepthId = toResourceId(depthId);

                ExportResult exp = exportTexture(session, rawDepthId, outputDir);
                if (!exp.outputPath.empty()) {
                    std::string depthPath = (fs::path(outputDir) / "depth.png").string();
                    try {
                        fs::rename(exp.outputPath, depthPath);
                        result.files.push_back(depthPath);
                    } catch (...) {
                        result.files.push_back(exp.outputPath);
                    }
                }
            } catch (const CoreError&) {
                // Depth export failed — not critical for snapshot.
            } catch (const std::exception& e) {
                result.errors.push_back(std::string("Depth export: ") + e.what());
            }
        }
    }

    // 5. Write manifest.json.
    {
        auto now = std::chrono::system_clock::now();
        auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();

        nlohmann::json manifest;
        manifest["eventId"] = eventId;
        manifest["timestamp"] = epoch;

        auto filesArr = nlohmann::json::array();
        for (const auto& f : result.files)
            filesArr.push_back(fs::path(f).filename().string());
        manifest["files"] = filesArr;

        if (!result.errors.empty())
            manifest["errors"] = result.errors;

        std::string manifestPath = (fs::path(outputDir) / "manifest.json").string();
        std::ofstream ofs(manifestPath);
        if (ofs) {
            ofs << manifest.dump(2);
            ofs.close();
            result.manifestPath = manifestPath;
            result.files.push_back(manifestPath);
        } else {
            result.errors.push_back("Failed to write manifest.json");
        }
    }

    return result;
}

} // namespace renderdoc::core
