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
#include <sstream>

namespace fs = std::filesystem;

namespace renderdoc::core {

namespace {

// Escape a string for JSON output (handles quotes, backslashes, control chars).
std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

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
    auto* ctrl = session.controller();
    if (!ctrl)
        throw CoreError(CoreError::Code::NoCaptureOpen,
                        "No capture is open. Call open_capture first.");

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
        } catch (...) {
            // No shader bound at this stage — skip silently.
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
                // The export already wrote with its own naming; rename it.
                try {
                    fs::rename(exp.outputPath, targetPath);
                    result.files.push_back(targetPath);
                } catch (...) {
                    // If rename fails, keep original path.
                    result.files.push_back(exp.outputPath);
                }
            }
        } catch (...) {
            // No RT at this index — skip.
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
            } catch (...) {
                // Depth export failed — not critical.
            }
        }
    }

    // 5. Write manifest.json (hand-crafted, no nlohmann::json dependency).
    {
        auto now = std::chrono::system_clock::now();
        auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();

        std::ostringstream manifest;
        manifest << "{\n";
        manifest << "  \"eventId\": " << eventId << ",\n";
        manifest << "  \"timestamp\": " << epoch << ",\n";
        manifest << "  \"files\": [";

        for (size_t i = 0; i < result.files.size(); i++) {
            if (i > 0) manifest << ",";
            // Use just the filename, not the full path.
            std::string fname = fs::path(result.files[i]).filename().string();
            manifest << "\n    \"" << jsonEscape(fname) << "\"";
        }

        manifest << "\n  ]";

        if (!result.errors.empty()) {
            manifest << ",\n  \"errors\": [";
            for (size_t i = 0; i < result.errors.size(); i++) {
                if (i > 0) manifest << ",";
                manifest << "\n    \"" << jsonEscape(result.errors[i]) << "\"";
            }
            manifest << "\n  ]";
        }

        manifest << "\n}\n";

        std::string manifestPath = (fs::path(outputDir) / "manifest.json").string();
        std::ofstream ofs(manifestPath);
        if (ofs) {
            ofs << manifest.str();
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
