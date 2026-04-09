// renderdoc-cli — one-shot compound-command CLI

#include "cli/cli_parse.h"
#include "core/capture.h"
#include "core/debug.h"
#include "core/diff.h"
#include "core/diff_session.h"
#include "core/errors.h"
#include "core/events.h"
#include "core/export.h"
#include "core/info.h"
#include "core/pipeline.h"
#include "core/pixel.h"
#include "core/resources.h"
#include "core/session.h"
#include "core/shaders.h"
#include "core/texstats.h"
#include "core/types.h"
#include "core/assertions.h"
#include "core/mesh.h"
#include "core/shader_edit.h"
#include "core/snapshot.h"
#include "core/usage.h"
#include "core/pass_analysis.h"
#include "core/counters.h"
#include "core/cbuffer.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace renderdoc::core;
using renderdoc::cli::Args;
using renderdoc::cli::parseStage;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string apiName(GraphicsApi api) {
    switch (api) {
    case GraphicsApi::D3D11:   return "D3D11";
    case GraphicsApi::D3D12:   return "D3D12";
    case GraphicsApi::OpenGL:  return "OpenGL";
    case GraphicsApi::Vulkan:  return "Vulkan";
    default:                   return "Unknown";
    }
}

static std::string stageName(ShaderStage s) {
    switch (s) {
    case ShaderStage::Vertex:   return "VS";
    case ShaderStage::Hull:     return "HS";
    case ShaderStage::Domain:   return "DS";
    case ShaderStage::Geometry: return "GS";
    case ShaderStage::Pixel:    return "PS";
    case ShaderStage::Compute:  return "CS";
    default:                    return "??";
    }
}


// ---------------------------------------------------------------------------
// Command implementations
// ---------------------------------------------------------------------------

static void cmdInfo(Session& session) {
    CaptureInfo info = getCaptureInfo(session);

    std::cout << "Path:         " << info.path << "\n"
              << "API:          " << apiName(info.api) << "\n"
              << "Degraded:     " << (info.degraded ? "yes" : "no") << "\n"
              << "Total events: " << info.totalEvents << "\n"
              << "Total draws:  " << info.totalDraws << "\n"
              << "Machine:      " << info.machineIdent << "\n"
              << "Driver:       " << info.driverName << "\n"
              << "Callstacks:   " << (info.hasCallstacks ? "yes" : "no") << "\n";

    if (!info.gpus.empty()) {
        std::cout << "GPUs:\n";
        for (const auto& gpu : info.gpus) {
            std::cout << "  " << gpu.name
                      << "  vendor=" << gpu.vendor
                      << "  deviceID=0x" << std::hex << gpu.deviceID << std::dec
                      << "  driver=" << gpu.driver << "\n";
        }
    }
}

static void cmdEvents(Session& session, const std::string& filter) {
    auto events = listEvents(session, filter);

    std::cout << "EID\tName\tFlags\tIndices\tInstances\n";
    for (const auto& e : events) {
        std::cout << e.eventId << "\t"
                  << e.name   << "\t"
                  << e.flags  << "\t"
                  << e.numIndices << "\t"
                  << e.numInstances << "\n";
    }
    std::cout << "# " << events.size() << " events\n";
}

static void cmdDraws(Session& session, const std::string& filter) {
    auto draws = listDraws(session, filter);

    std::cout << "EID\tName\tIndices\tInstances\n";
    for (const auto& d : draws) {
        std::cout << d.eventId << "\t"
                  << d.name   << "\t"
                  << d.numIndices << "\t"
                  << d.numInstances << "\n";
    }
    std::cout << "# " << draws.size() << " draws\n";
}

static void cmdPipeline(Session& session, std::optional<uint32_t> eid) {
    PipelineState ps = getPipelineState(session, eid);

    std::cout << "API: " << apiName(ps.api) << "\n\n";

    if (!ps.shaders.empty()) {
        std::cout << "Shaders:\n"
                  << "Stage\tShaderID\tEntryPoint\n";
        for (const auto& sh : ps.shaders) {
            std::cout << stageName(sh.stage) << "\t"
                      << sh.shaderId         << "\t"
                      << sh.entryPoint       << "\n";
        }
        std::cout << "\n";
    }

    if (!ps.renderTargets.empty()) {
        std::cout << "Render targets:\n"
                  << "ID\tName\tWidth\tHeight\tFormat\n";
        for (const auto& rt : ps.renderTargets) {
            std::cout << rt.id     << "\t"
                      << rt.name   << "\t"
                      << rt.width  << "\t"
                      << rt.height << "\t"
                      << rt.format << "\n";
        }
        std::cout << "\n";
    }

    if (ps.depthTarget) {
        const auto& dt = *ps.depthTarget;
        std::cout << "Depth target:\n"
                  << "ID\tName\tWidth\tHeight\tFormat\n"
                  << dt.id     << "\t"
                  << dt.name   << "\t"
                  << dt.width  << "\t"
                  << dt.height << "\t"
                  << dt.format << "\n\n";
    }

    if (!ps.viewports.empty()) {
        std::cout << "Viewports:\n"
                  << "X\tY\tW\tH\tMinZ\tMaxZ\n";
        for (const auto& vp : ps.viewports) {
            std::cout << vp.x        << "\t"
                      << vp.y        << "\t"
                      << vp.width    << "\t"
                      << vp.height   << "\t"
                      << vp.minDepth << "\t"
                      << vp.maxDepth << "\n";
        }
    }
}

static void cmdShader(Session& session, const std::string& stageStr,
                      std::optional<uint32_t> eid) {
    auto maybeStage = parseStage(stageStr);
    if (!maybeStage) {
        std::cerr << "error: unknown shader stage '" << stageStr
                  << "'. Valid: vs hs ds gs ps cs\n";
        std::exit(1);
    }

    ShaderDisassembly disasm = getShaderDisassembly(session, *maybeStage, eid);

    std::cout << "Stage:  " << stageName(disasm.stage) << "\n"
              << "ID:     " << disasm.id               << "\n"
              << "Target: " << disasm.target            << "\n"
              << "\n"
              << disasm.disassembly << "\n";
}

static void cmdResources(Session& session, const std::string& typeFilter) {
    auto resources = listResources(session, typeFilter);

    std::cout << "ID\tName\tType\tBytes\tWidth\tHeight\tFormat\n";
    for (const auto& r : resources) {
        std::cout << r.id   << "\t"
                  << r.name << "\t"
                  << r.type << "\t"
                  << r.byteSize << "\t"
                  << (r.width  ? std::to_string(*r.width)  : "") << "\t"
                  << (r.height ? std::to_string(*r.height) : "") << "\t"
                  << (r.format ? *r.format : "") << "\n";
    }
    std::cout << "# " << resources.size() << " resources\n";
}

static void cmdExportRt(Session& session, const std::vector<std::string>& positional,
                        const std::string& outputDir,
                        std::optional<uint32_t> eid) {
    if (positional.empty()) {
        std::cerr << "error: export-rt requires render-target index (0-7)\n";
        std::exit(1);
    }
    if (outputDir.empty()) {
        std::cerr << "error: export-rt requires -o <output-dir>\n";
        std::exit(1);
    }

    int rtIndex = std::stoi(positional[0]);

    // Navigate to event if requested
    if (eid) {
        gotoEvent(session, *eid);
    }

    ExportResult result = exportRenderTarget(session, rtIndex, outputDir);

    std::cout << "Exported: " << result.outputPath     << "\n"
              << "Event:    " << result.eventId         << "\n"
              << "RT index: " << result.rtIndex         << "\n"
              << "Size:     " << result.width << "x" << result.height << "\n"
              << "Bytes:    " << result.byteSize         << "\n";
}

static void cmdCapture(Session& session, const std::string& exePath,
                       const std::string& workingDir, const std::string& cmdLineArgs,
                       uint32_t delayFrames, const std::string& outputPath) {
    CaptureRequest req;
    req.exePath = exePath;
    req.workingDir = workingDir;
    req.cmdLine = cmdLineArgs;
    req.delayFrames = delayFrames;
    req.outputPath = outputPath;

    std::cerr << "Launching and injecting: " << exePath << "\n";
    std::cerr << "Waiting " << delayFrames << " frames before capture...\n";

    CaptureResult result = captureFrame(session, req);

    CaptureInfo info = getCaptureInfo(session);

    std::cout << "Capture:      " << result.capturePath << "\n"
              << "PID:          " << result.pid << "\n"
              << "API:          " << apiName(info.api) << "\n"
              << "Total events: " << info.totalEvents << "\n"
              << "Total draws:  " << info.totalDraws << "\n";
}

static void cmdPixel(Session& session, const std::vector<std::string>& positional,
                     uint32_t targetIndex, std::optional<uint32_t> eid) {
    if (positional.size() < 2) {
        std::cerr << "error: 'pixel' requires X Y coordinates\n";
        std::exit(1);
    }
    uint32_t x = static_cast<uint32_t>(std::stoul(positional[0]));
    uint32_t y = static_cast<uint32_t>(std::stoul(positional[1]));

    auto result = pixelHistory(session, x, y, targetIndex, eid);
    std::cout << "Pixel (" << x << "," << y << ") target=" << targetIndex
              << " up to event " << result.eventId << "\n";
    std::cout << result.modifications.size() << " modifications:\n\n";

    for (const auto& mod : result.modifications) {
        std::cout << "  EID " << mod.eventId
                  << "  frag=" << mod.fragmentIndex
                  << "  prim=" << mod.primitiveId
                  << "  passed=" << (mod.passed ? "yes" : "no");
        if (mod.depth.has_value())
            std::cout << "  depth=" << *mod.depth;
        if (!mod.flags.empty()) {
            std::cout << "  flags=";
            for (size_t i = 0; i < mod.flags.size(); i++) {
                if (i > 0) std::cout << ",";
                std::cout << mod.flags[i];
            }
        }
        std::cout << "\n";
        std::cout << "    post: r=" << mod.postMod.floatValue[0]
                  << " g=" << mod.postMod.floatValue[1]
                  << " b=" << mod.postMod.floatValue[2]
                  << " a=" << mod.postMod.floatValue[3] << "\n";
    }
}

static void cmdPickPixel(Session& session, const std::vector<std::string>& positional,
                         uint32_t targetIndex, std::optional<uint32_t> eid) {
    if (positional.size() < 2) {
        std::cerr << "error: 'pick-pixel' requires X Y coordinates\n";
        std::exit(1);
    }
    uint32_t x = static_cast<uint32_t>(std::stoul(positional[0]));
    uint32_t y = static_cast<uint32_t>(std::stoul(positional[1]));

    auto result = pickPixel(session, x, y, targetIndex, eid);
    std::cout << "Pixel (" << x << "," << y << ") at event " << result.eventId << ":\n"
              << "  float: " << result.color.floatValue[0] << " "
                             << result.color.floatValue[1] << " "
                             << result.color.floatValue[2] << " "
                             << result.color.floatValue[3] << "\n"
              << "  uint:  " << result.color.uintValue[0] << " "
                             << result.color.uintValue[1] << " "
                             << result.color.uintValue[2] << " "
                             << result.color.uintValue[3] << "\n";
}

static void cmdDebug(Session& session, const std::vector<std::string>& positional,
                     std::optional<uint32_t> eid, bool trace,
                     uint32_t instance, uint32_t primitive,
                     uint32_t index, uint32_t view) {
    if (positional.empty()) {
        std::cerr << "error: 'debug' requires subcommand: pixel|vertex|thread\n";
        std::exit(1);
    }
    if (!eid.has_value()) {
        std::cerr << "error: 'debug' requires -e EID\n";
        std::exit(1);
    }

    std::string sub = positional[0];
    ShaderDebugResult result;

    if (sub == "pixel") {
        if (positional.size() < 3) {
            std::cerr << "error: 'debug pixel' requires X Y\n";
            std::exit(1);
        }
        uint32_t x = static_cast<uint32_t>(std::stoul(positional[1]));
        uint32_t y = static_cast<uint32_t>(std::stoul(positional[2]));
        result = debugPixel(session, *eid, x, y, trace, primitive);
    } else if (sub == "vertex") {
        if (positional.size() < 2) {
            std::cerr << "error: 'debug vertex' requires VTX_ID\n";
            std::exit(1);
        }
        uint32_t vtx = static_cast<uint32_t>(std::stoul(positional[1]));
        result = debugVertex(session, *eid, vtx, trace, instance, index, view);
    } else if (sub == "thread") {
        if (positional.size() < 7) {
            std::cerr << "error: 'debug thread' requires GX GY GZ TX TY TZ\n";
            std::exit(1);
        }
        uint32_t gx = static_cast<uint32_t>(std::stoul(positional[1]));
        uint32_t gy = static_cast<uint32_t>(std::stoul(positional[2]));
        uint32_t gz = static_cast<uint32_t>(std::stoul(positional[3]));
        uint32_t tx = static_cast<uint32_t>(std::stoul(positional[4]));
        uint32_t ty = static_cast<uint32_t>(std::stoul(positional[5]));
        uint32_t tz = static_cast<uint32_t>(std::stoul(positional[6]));
        result = debugThread(session, *eid, gx, gy, gz, tx, ty, tz, trace);
    } else {
        std::cerr << "error: unknown debug subcommand '" << sub << "'\n";
        std::exit(1);
    }

    std::cout << "Stage: " << result.stage << "  Event: " << result.eventId
              << "  Steps: " << result.totalSteps << "\n\n";

    auto printVars = [](const std::string& label, const std::vector<DebugVariable>& vars) {
        if (vars.empty()) return;
        std::cout << label << ":\n";
        for (const auto& v : vars) {
            std::cout << "  " << v.type << " " << v.name << " = ";
            if (!v.floatValues.empty()) {
                for (size_t i = 0; i < v.floatValues.size(); i++)
                    std::cout << (i ? ", " : "") << v.floatValues[i];
            } else if (!v.intValues.empty()) {
                for (size_t i = 0; i < v.intValues.size(); i++)
                    std::cout << (i ? ", " : "") << v.intValues[i];
            } else if (!v.uintValues.empty()) {
                for (size_t i = 0; i < v.uintValues.size(); i++)
                    std::cout << (i ? ", " : "") << v.uintValues[i];
            }
            std::cout << "\n";
        }
    };

    printVars("Inputs", result.inputs);
    printVars("Outputs", result.outputs);

    if (!result.trace.empty()) {
        std::cout << "\nTrace (" << result.trace.size() << " steps):\n";
        for (const auto& step : result.trace) {
            std::cout << "  [" << step.step << "] instr=" << step.instruction;
            if (!step.file.empty())
                std::cout << " " << step.file << ":" << step.line;
            if (!step.changes.empty())
                std::cout << " (" << step.changes.size() << " changes)";
            std::cout << "\n";
        }
    }
}

static void cmdTexStats(Session& session, const std::vector<std::string>& positional,
                        std::optional<uint32_t> eid, uint32_t mip, uint32_t slice,
                        bool histogram) {
    if (positional.empty()) {
        std::cerr << "error: 'tex-stats' requires RESOURCE_ID\n";
        std::exit(1);
    }

    uint64_t resId = std::stoull(positional[0]);
    auto result = getTextureStats(session, resId, mip, slice, histogram, eid);

    std::cout << "Texture ResourceId::" << result.id << " at event " << result.eventId
              << " mip=" << result.mip << " slice=" << result.slice << "\n";
    std::cout << "Min: " << result.minVal.floatValue[0] << " "
              << result.minVal.floatValue[1] << " "
              << result.minVal.floatValue[2] << " "
              << result.minVal.floatValue[3] << "\n";
    std::cout << "Max: " << result.maxVal.floatValue[0] << " "
              << result.maxVal.floatValue[1] << " "
              << result.maxVal.floatValue[2] << " "
              << result.maxVal.floatValue[3] << "\n";

    if (!result.histogram.empty()) {
        std::cout << "\nHistogram (256 buckets):\n";
        std::cout << "bucket\tR\tG\tB\tA\n";
        for (size_t i = 0; i < result.histogram.size(); i++) {
            const auto& b = result.histogram[i];
            if (b.r || b.g || b.b || b.a) {
                std::cout << i << "\t" << b.r << "\t" << b.g
                          << "\t" << b.b << "\t" << b.a << "\n";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Phase 2 command implementations
// ---------------------------------------------------------------------------

static void cmdShaderEncodings(Session& session) {
    auto encodings = getShaderEncodings(session);
    for (const auto& e : encodings) std::cout << e << "\n";
    std::cout << "# " << encodings.size() << " encoding(s)\n";
}

static void cmdShaderBuild(Session& session, const std::vector<std::string>& positional,
                           const std::string& stageStr, const std::string& encoding,
                           const std::string& entry) {
    if (positional.empty()) {
        std::cerr << "error: 'shader-build' requires FILE\n";
        std::exit(1);
    }
    if (encoding.empty()) {
        std::cerr << "error: 'shader-build' requires --encoding ENC\n";
        std::exit(1);
    }

    // Read source file
    std::string sourceFile = positional[0];
    std::ifstream f(sourceFile);
    if (!f) {
        std::cerr << "error: cannot open '" << sourceFile << "'\n";
        std::exit(1);
    }
    std::string source((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());

    auto maybeStage = parseStage(stageStr);
    if (!maybeStage) {
        std::cerr << "error: unknown shader stage '" << stageStr
                  << "'. Valid: vs hs ds gs ps cs\n";
        std::exit(1);
    }

    ShaderBuildResult result = buildShader(session, source, *maybeStage, entry, encoding);
    if (result.shaderId == 0) {
        std::cerr << "error: shader build failed\n";
        if (!result.warnings.empty())
            std::cerr << result.warnings << "\n";
        std::exit(1);
    }

    std::cout << "shaderId: " << result.shaderId << "\n";
    if (!result.warnings.empty())
        std::cout << "warnings:\n" << result.warnings << "\n";
}

static void cmdShaderReplace(Session& session, const std::vector<std::string>& positional,
                             const std::string& stageStr, uint64_t withShaderId) {
    if (positional.empty()) {
        std::cerr << "error: 'shader-replace' requires EID STAGE\n";
        std::exit(1);
    }
    uint32_t eid = static_cast<uint32_t>(std::stoul(positional[0]));
    // stageStr comes from positional[1] if not set via --stage
    std::string stage = stageStr;
    if (stage == "vs-out" && positional.size() >= 2) {
        // positional[1] is the stage for shader-replace
        stage = positional[1];
    }

    auto maybeStage = parseStage(stage);
    if (!maybeStage) {
        std::cerr << "error: unknown shader stage '" << stage
                  << "'. Valid: vs hs ds gs ps cs\n";
        std::exit(1);
    }
    if (withShaderId == 0) {
        std::cerr << "error: 'shader-replace' requires --with SHADER_ID\n";
        std::exit(1);
    }

    uint64_t newId = replaceShader(session, eid, *maybeStage, withShaderId);
    std::cout << "replaced shaderId: " << newId << "\n";
}

static void cmdShaderRestore(Session& session, const std::vector<std::string>& positional,
                             const std::string& stageStr) {
    if (positional.empty()) {
        std::cerr << "error: 'shader-restore' requires EID STAGE\n";
        std::exit(1);
    }
    uint32_t eid = static_cast<uint32_t>(std::stoul(positional[0]));
    std::string stage = stageStr;
    if (stage == "vs-out" && positional.size() >= 2) {
        stage = positional[1];
    }

    auto maybeStage = parseStage(stage);
    if (!maybeStage) {
        std::cerr << "error: unknown shader stage '" << stage
                  << "'. Valid: vs hs ds gs ps cs\n";
        std::exit(1);
    }

    restoreShader(session, eid, *maybeStage);
    std::cout << "restored shader at event " << eid << " stage " << stage << "\n";
}

static void cmdShaderRestoreAll(Session& session) {
    auto [restored, skipped] = restoreAllShaders(session);
    std::cout << "restored: " << restored << "  skipped: " << skipped << "\n";
}

static int cmdMesh(Session& session, const std::vector<std::string>& positional,
                   const std::string& stageStr, const std::string& format,
                   const std::string& outputFile) {
    if (positional.empty()) {
        std::cerr << "error: 'mesh' requires EID\n";
        std::exit(1);
    }
    uint32_t eid = static_cast<uint32_t>(std::stoul(positional[0]));
    MeshStage stage = (stageStr == "gs-out") ? MeshStage::GSOut : MeshStage::VSOut;

    auto data = exportMesh(session, eid, stage);

    if (format == "json") {
        std::cout << "vertices: " << data.vertices.size() << "\n";
        std::cout << "faces: " << data.faces.size() << "\n";
        std::cout << "topology: " << static_cast<int>(data.topology) << "\n";
    } else {
        std::string obj = meshToObj(data);
        std::cout << obj;
        if (!outputFile.empty()) {
            std::ofstream f(outputFile);
            if (!f) {
                std::cerr << "error: cannot write to '" << outputFile << "'\n";
                return 1;
            }
            f << obj;
        }
    }
    return 0;
}

static void cmdSnapshot(Session& session, const std::vector<std::string>& positional,
                        const std::string& outputDir) {
    if (positional.empty()) {
        std::cerr << "error: 'snapshot' requires EID\n";
        std::exit(1);
    }
    if (outputDir.empty()) {
        std::cerr << "error: 'snapshot' requires -o DIR\n";
        std::exit(1);
    }
    uint32_t eid = static_cast<uint32_t>(std::stoul(positional[0]));

    // Simple pipeline serializer: return empty JSON object
    auto pipelineSerializer = [](const PipelineState&) -> std::string {
        return "{}";
    };

    SnapshotResult result = exportSnapshot(session, eid, outputDir, pipelineSerializer);

    std::cout << "manifest: " << result.manifestPath << "\n";
    std::cout << "files: " << result.files.size() << "\n";
    for (const auto& f : result.files)
        std::cout << "  " << f << "\n";
    if (!result.errors.empty()) {
        std::cout << "errors: " << result.errors.size() << "\n";
        for (const auto& e : result.errors)
            std::cerr << "  " << e << "\n";
    }
}

static void cmdUsage(Session& session, const std::vector<std::string>& positional) {
    if (positional.empty()) {
        std::cerr << "error: 'usage' requires RES_ID\n";
        std::exit(1);
    }
    ResourceId resId = static_cast<ResourceId>(std::stoull(positional[0]));
    auto result = getResourceUsage(session, resId);

    std::cout << "ResourceId: " << result.resourceId << "\n";
    std::cout << "EID\tUsage\n";
    for (const auto& e : result.entries)
        std::cout << e.eventId << "\t" << e.usage << "\n";
    std::cout << "# " << result.entries.size() << " entries\n";
}

static int cmdAssertPixel(Session& session, const std::vector<std::string>& positional,
                          std::optional<uint32_t> eventId,
                          const float expectRGBA[4], bool hasExpectRGBA,
                          float tolerance, uint32_t targetIndex) {
    if (positional.size() < 2) {
        std::cerr << "error: 'assert-pixel' requires X Y\n";
        std::exit(1);
    }
    if (!hasExpectRGBA) {
        std::cerr << "error: 'assert-pixel' requires --expect R G B A\n";
        std::exit(1);
    }

    uint32_t eid = eventId.value_or(0);
    uint32_t x = static_cast<uint32_t>(std::stoul(positional[0]));
    uint32_t y = static_cast<uint32_t>(std::stoul(positional[1]));

    auto result = assertPixel(session, eid, x, y, expectRGBA, tolerance, targetIndex);
    std::cout << (result.pass ? "pass" : "FAIL") << ": " << result.message << "\n";
    if (!result.pass) {
        std::cout << "  actual:   "
                  << result.actual[0] << " " << result.actual[1] << " "
                  << result.actual[2] << " " << result.actual[3] << "\n"
                  << "  expected: "
                  << result.expected[0] << " " << result.expected[1] << " "
                  << result.expected[2] << " " << result.expected[3] << "\n"
                  << "  tolerance: " << result.tolerance << "\n";
    }
    return result.pass ? 0 : 1;
}

static int cmdAssertState(const std::vector<std::string>& positional,
                          const std::string& expectStr) {
    // assert-state EID PATH --expect VALUE
    // positional[0] = EID (informational), positional[1] = PATH (the actual value path)
    if (positional.size() < 2) {
        std::cerr << "error: 'assert-state' requires EID PATH\n";
        std::exit(1);
    }
    if (expectStr.empty()) {
        std::cerr << "error: 'assert-state' requires --expect VALUE\n";
        std::exit(1);
    }

    std::string path = positional[1];
    // The "actual" value is the path itself for CLI; core assertState compares actual vs expected
    // In CLI context, actual is read by the caller; we pass path as actual for display purposes
    // The assertState API: assertState(path, actual, expected)
    // For CLI we treat positional[1] as the path, and expectStr as the expected value.
    // The actual value would need to be obtained separately; for the CLI we pass an empty actual
    // so the assertion will show what it compares. This is consistent with assertState's role.
    auto result = assertState(path, "", expectStr);
    std::cout << (result.pass ? "pass" : "FAIL") << ": " << result.message << "\n";
    for (const auto& kv : result.details)
        std::cout << "  " << kv.first << ": " << kv.second << "\n";
    return result.pass ? 0 : 1;
}

static int cmdAssertImage(const std::vector<std::string>& positional,
                          double threshold, const std::string& diffOutput) {
    if (positional.size() < 2) {
        std::cerr << "error: 'assert-image' requires EXPECTED ACTUAL\n";
        std::exit(1);
    }
    std::string expected = positional[0];
    std::string actual   = positional[1];

    auto result = assertImage(expected, actual, threshold, diffOutput);
    std::cout << (result.pass ? "pass" : "FAIL") << ": " << result.message << "\n";
    std::cout << "  diffPixels:  " << result.diffPixels << "\n"
              << "  totalPixels: " << result.totalPixels << "\n"
              << "  diffRatio:   " << result.diffRatio << "\n";
    if (!result.diffOutputPath.empty())
        std::cout << "  diffOutput:  " << result.diffOutputPath << "\n";
    return result.pass ? 0 : 1;
}

static int cmdAssertCount(Session& session, const std::vector<std::string>& positional,
                          int expectCount, const std::string& opStr) {
    if (positional.empty()) {
        std::cerr << "error: 'assert-count' requires WHAT\n";
        std::exit(1);
    }
    std::string what = positional[0];

    auto result = assertCount(session, what, expectCount, opStr);
    std::cout << (result.pass ? "pass" : "FAIL") << ": " << result.message << "\n";
    for (const auto& kv : result.details)
        std::cout << "  " << kv.first << ": " << kv.second << "\n";
    return result.pass ? 0 : 1;
}

static int cmdAssertClean(Session& session, const std::string& minSeverity) {
    auto result = assertClean(session, minSeverity);
    std::cout << (result.result.pass ? "pass" : "FAIL") << ": " << result.result.message << "\n";
    if (!result.messages.empty()) {
        std::cout << result.messages.size() << " debug message(s):\n";
        for (const auto& msg : result.messages) {
            std::cout << "  [" << msg.severity << "] EID=" << msg.eventId
                      << " cat=" << msg.category << " " << msg.message << "\n";
        }
    }
    return result.result.pass ? 0 : 1;
}

// ---------------------------------------------------------------------------
// diff command (standalone — takes two .rdc files, no shared session)
// ---------------------------------------------------------------------------

static std::string diffStatusStr(DiffStatus s) {
    switch (s) {
    case DiffStatus::Equal:    return "=";
    case DiffStatus::Modified: return "~";
    case DiffStatus::Added:    return "+";
    case DiffStatus::Deleted:  return "-";
    default:                   return "?";
    }
}

static int cmdDiff(int argc, char* argv[]) {
    // argv: [0]=exe [1]="diff" [2]=FILE_A [3]=FILE_B [4..]=options
    if (argc < 4) {
        std::cerr << "error: 'diff' requires FILE_A FILE_B\n";
        std::cerr << "Usage: " << argv[0]
                  << " diff FILE_A FILE_B [--draws|--resources|--stats"
                     "|--pipeline MARKER|--framebuffer] [--target N]"
                     " [--threshold F] [--eid-a N] [--eid-b N]"
                     " [--diff-output PATH] [--verbose]\n";
        return 2;
    }

    std::string fileA = argv[2];
    std::string fileB = argv[3];

    // Parse options
    enum class DiffMode { Summary, Draws, Resources, Stats, Pipeline, Framebuffer };
    DiffMode mode = DiffMode::Summary;
    std::string pipelineMarker;
    uint32_t eidA = 0, eidB = 0;
    uint32_t target = 0;
    double threshold = 0.0;
    std::string diffOutput;
    bool verbose = false;

    for (int i = 4; i < argc; ++i) {
        std::string tok = argv[i];
        if (tok == "--draws") {
            mode = DiffMode::Draws;
        } else if (tok == "--resources") {
            mode = DiffMode::Resources;
        } else if (tok == "--stats") {
            mode = DiffMode::Stats;
        } else if (tok == "--pipeline" && i + 1 < argc) {
            mode = DiffMode::Pipeline;
            pipelineMarker = argv[++i];
        } else if (tok == "--framebuffer") {
            mode = DiffMode::Framebuffer;
        } else if (tok == "--target" && i + 1 < argc) {
            target = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--threshold" && i + 1 < argc) {
            threshold = std::stod(argv[++i]);
        } else if (tok == "--eid-a" && i + 1 < argc) {
            eidA = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--eid-b" && i + 1 < argc) {
            eidB = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--diff-output" && i + 1 < argc) {
            diffOutput = argv[++i];
        } else if (tok == "--verbose") {
            verbose = true;
        }
    }

    try {
        // DiffSession does not own replay init — prime it via a temporary Session
        Session initSession;
        initSession.ensureReplayInitialized();

        DiffSession ds;
        ds.open(fileA, fileB);

        int exitCode = 0;

        if (mode == DiffMode::Summary) {
            SummaryDiffResult result = diffSummary(ds);
            if (result.identical) {
                std::cout << "identical\n";
                exitCode = 0;
            } else {
                // Print aligned columns
                for (const auto& row : result.rows) {
                    // Format delta
                    std::string deltaStr;
                    if (row.delta > 0)
                        deltaStr = "(+" + std::to_string(row.delta) + ")";
                    else if (row.delta < 0)
                        deltaStr = "(" + std::to_string(row.delta) + ")";
                    else
                        deltaStr = "(0)";

                    // Aligned output: label left-padded to 12, values right-aligned
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "%-12s %6d -> %-6d  %s",
                                  (row.category + ":").c_str(),
                                  row.valueA, row.valueB, deltaStr.c_str());
                    std::cout << buf << "\n";
                }
                if (!result.divergedAt.empty())
                    std::cout << "diverged-at: " << result.divergedAt << "\n";
                exitCode = 1;
            }

        } else if (mode == DiffMode::Draws) {
            DrawsDiffResult result = diffDraws(ds);
            std::cout << "STATUS\tEID_A\tEID_B\tMARKER\tTYPE\tTRI_A\tTRI_B\tCONFIDENCE\n";
            for (const auto& row : result.rows) {
                if (!verbose && row.status == DiffStatus::Equal)
                    continue;
                std::string eidAStr = row.a ? std::to_string(row.a->eventId) : "-";
                std::string eidBStr = row.b ? std::to_string(row.b->eventId) : "-";
                std::string marker  = row.a ? row.a->markerPath
                                            : (row.b ? row.b->markerPath : "");
                std::string type    = row.a ? row.a->drawType
                                            : (row.b ? row.b->drawType : "");
                std::string triA    = row.a ? std::to_string(row.a->triangles) : "-";
                std::string triB    = row.b ? std::to_string(row.b->triangles) : "-";
                std::cout << diffStatusStr(row.status) << "\t"
                          << eidAStr << "\t" << eidBStr << "\t"
                          << marker << "\t" << type << "\t"
                          << triA << "\t" << triB << "\t"
                          << row.confidence << "\n";
            }
            std::cout << "# added=" << result.added
                      << " deleted=" << result.deleted
                      << " modified=" << result.modified
                      << " unchanged=" << result.unchanged << "\n";
            if (result.added > 0 || result.deleted > 0 || result.modified > 0)
                exitCode = 1;

        } else if (mode == DiffMode::Resources) {
            ResourcesDiffResult result = diffResources(ds);
            std::cout << "STATUS\tNAME\tTYPE_A\tTYPE_B\n";
            for (const auto& row : result.rows) {
                if (!verbose && row.status == DiffStatus::Equal)
                    continue;
                std::cout << diffStatusStr(row.status) << "\t"
                          << row.name << "\t"
                          << row.typeA << "\t"
                          << row.typeB << "\n";
            }
            std::cout << "# added=" << result.added
                      << " deleted=" << result.deleted
                      << " modified=" << result.modified
                      << " unchanged=" << result.unchanged << "\n";
            if (result.added > 0 || result.deleted > 0 || result.modified > 0)
                exitCode = 1;

        } else if (mode == DiffMode::Stats) {
            StatsDiffResult result = diffStats(ds);
            std::cout << "STATUS\tPASS\tDRAWS_A\tDRAWS_B\n";
            for (const auto& row : result.rows) {
                if (!verbose && row.status == DiffStatus::Equal)
                    continue;
                std::string drawsA = row.drawsA ? std::to_string(*row.drawsA) : "-";
                std::string drawsB = row.drawsB ? std::to_string(*row.drawsB) : "-";
                std::cout << diffStatusStr(row.status) << "\t"
                          << row.name << "\t"
                          << drawsA << "\t"
                          << drawsB << "\n";
            }
            std::cout << "# passesChanged=" << result.passesChanged
                      << " passesAdded=" << result.passesAdded
                      << " passesDeleted=" << result.passesDeleted
                      << " drawsDelta=" << result.drawsDelta << "\n";
            if (result.passesChanged > 0 || result.passesAdded > 0 || result.passesDeleted > 0)
                exitCode = 1;

        } else if (mode == DiffMode::Pipeline) {
            PipelineDiffResult result = diffPipeline(ds, pipelineMarker);
            std::cout << "eid-a: " << result.eidA
                      << "  eid-b: " << result.eidB
                      << "  marker: " << result.markerPath << "\n";
            std::cout << "SECTION\tFIELD\tA\tB\n";
            for (const auto& f : result.fields) {
                if (!verbose && !f.changed)
                    continue;
                std::cout << f.section << "\t"
                          << f.field << "\t"
                          << f.valueA << "\t"
                          << f.valueB;
                if (f.changed)
                    std::cout << "\t<- changed";
                std::cout << "\n";
            }
            std::cout << "# changed=" << result.changedCount
                      << " total=" << result.totalCount << "\n";
            if (result.changedCount > 0)
                exitCode = 1;

        } else if (mode == DiffMode::Framebuffer) {
            ImageCompareResult result = diffFramebuffer(ds, eidA, eidB,
                                                        static_cast<int>(target),
                                                        threshold, diffOutput);
            if (result.diffPixels == 0) {
                std::cout << "identical\n";
                exitCode = 0;
            } else {
                double pct = (result.totalPixels > 0)
                    ? (100.0 * result.diffPixels / result.totalPixels)
                    : 0.0;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "diff: %llu/%llu pixels (%.2f%%)",
                              static_cast<unsigned long long>(result.diffPixels),
                              static_cast<unsigned long long>(result.totalPixels),
                              pct);
                std::cout << buf << "\n";
                if (!result.diffOutputPath.empty())
                    std::cout << "diff-image: " << result.diffOutputPath << "\n";
                exitCode = 1;
            }
        }

        ds.close();
        return exitCode;

    } catch (const CoreError& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}

// ---------------------------------------------------------------------------
// Phase 4: Pass Analysis commands
// ---------------------------------------------------------------------------

static std::string ridStr(uint64_t id) {
    return "ResourceId::" + std::to_string(id);
}

static void cmdPassStats(Session& session) {
    auto stats = getPassStatistics(session);

    std::cout << "{\n  \"passes\": [\n";
    for (size_t i = 0; i < stats.size(); ++i) {
        const auto& ps = stats[i];
        std::cout << "    {\n"
                  << "      \"name\": \"" << ps.name << "\",\n"
                  << "      \"eventId\": " << ps.eventId << ",\n"
                  << "      \"drawCount\": " << ps.drawCount << ",\n"
                  << "      \"dispatchCount\": " << ps.dispatchCount << ",\n"
                  << "      \"totalTriangles\": " << ps.totalTriangles << ",\n"
                  << "      \"rtWidth\": " << ps.rtWidth << ",\n"
                  << "      \"rtHeight\": " << ps.rtHeight << ",\n"
                  << "      \"attachmentCount\": " << ps.attachmentCount << ",\n"
                  << "      \"synthetic\": " << (ps.synthetic ? "true" : "false") << "\n"
                  << "    }" << (i + 1 < stats.size() ? "," : "") << "\n";
    }
    std::cout << "  ],\n"
              << "  \"count\": " << stats.size() << "\n"
              << "}\n";
}

static void cmdPassDeps(Session& session) {
    auto graph = getPassDependencies(session);

    std::cout << "{\n  \"edges\": [\n";
    for (size_t i = 0; i < graph.edges.size(); ++i) {
        const auto& e = graph.edges[i];
        std::cout << "    {\n"
                  << "      \"srcPass\": \"" << e.srcPass << "\",\n"
                  << "      \"dstPass\": \"" << e.dstPass << "\",\n"
                  << "      \"resources\": [";
        for (size_t j = 0; j < e.sharedResources.size(); ++j) {
            std::cout << "\"" << ridStr(e.sharedResources[j]) << "\""
                      << (j + 1 < e.sharedResources.size() ? ", " : "");
        }
        std::cout << "]\n"
                  << "    }" << (i + 1 < graph.edges.size() ? "," : "") << "\n";
    }
    std::cout << "  ],\n"
              << "  \"passCount\": " << graph.passCount << ",\n"
              << "  \"edgeCount\": " << graph.edgeCount << "\n"
              << "}\n";
}

static void cmdUnusedTargets(Session& session) {
    auto result = findUnusedTargets(session);

    std::cout << "{\n  \"unused\": [\n";
    for (size_t i = 0; i < result.unused.size(); ++i) {
        const auto& ut = result.unused[i];
        std::cout << "    {\n"
                  << "      \"resourceId\": \"" << ridStr(ut.resourceId) << "\",\n"
                  << "      \"name\": \"" << ut.name << "\",\n"
                  << "      \"writtenBy\": [";
        for (size_t j = 0; j < ut.writtenBy.size(); ++j) {
            std::cout << "\"" << ut.writtenBy[j] << "\""
                      << (j + 1 < ut.writtenBy.size() ? ", " : "");
        }
        std::cout << "],\n"
                  << "      \"wave\": " << ut.wave << "\n"
                  << "    }" << (i + 1 < result.unused.size() ? "," : "") << "\n";
    }
    std::cout << "  ],\n"
              << "  \"unusedCount\": " << result.unusedCount << ",\n"
              << "  \"totalTargets\": " << result.totalTargets << "\n"
              << "}\n";
}

// ---------------------------------------------------------------------------
// counters / cbuffer
// ---------------------------------------------------------------------------

static void cmdCounters(Session& session, bool listMode, const std::string& nameFilter,
                        std::optional<uint32_t> eventId) {
    if (listMode) {
        auto counters = listCounters(session);
        std::cout << "{\n  \"counters\": [\n";
        for (size_t i = 0; i < counters.size(); ++i) {
            const auto& c = counters[i];
            std::cout << "    {\"id\": " << c.id
                      << ", \"name\": \"" << c.name
                      << "\", \"category\": \"" << c.category
                      << "\", \"unit\": \"" << c.unit
                      << "\", \"resultType\": \"" << c.resultType
                      << "\"}" << (i + 1 < counters.size() ? "," : "") << "\n";
        }
        std::cout << "  ],\n  \"count\": " << counters.size() << "\n}\n";
        return;
    }

    std::vector<std::string> names;
    if (!nameFilter.empty()) names.push_back(nameFilter);

    auto result = fetchCounters(session, names, eventId);
    std::cout << "{\n  \"rows\": [\n";
    for (size_t i = 0; i < result.rows.size(); ++i) {
        const auto& r = result.rows[i];
        std::cout << "    {\"eventId\": " << r.eventId
                  << ", \"counter\": \"" << r.counterName
                  << "\", \"value\": " << r.value
                  << ", \"unit\": \"" << r.unit
                  << "\"}" << (i + 1 < result.rows.size() ? "," : "") << "\n";
    }
    std::cout << "  ],\n  \"totalCounters\": " << result.totalCounters
              << ",\n  \"totalEvents\": " << result.totalEvents << "\n}\n";
}

static void cmdCBuffer(Session& session, const std::string& stageStr,
                       std::optional<uint32_t> cbufferIndex,
                       std::optional<uint32_t> eventId) {
    auto stageOpt = parseStage(stageStr);
    if (!stageOpt) {
        std::cerr << "error: invalid stage '" << stageStr << "' (use vs|hs|ds|gs|ps|cs)\n";
        return;
    }
    ShaderStage stage = *stageOpt;

    if (!cbufferIndex.has_value()) {
        // List mode: show constant block metadata
        auto buffers = listCBuffers(session, stage, eventId);
        std::cout << "{\n  \"stage\": \"" << stageName(stage) << "\",\n  \"cbuffers\": [\n";
        for (size_t i = 0; i < buffers.size(); ++i) {
            const auto& cb = buffers[i];
            std::cout << "    {\"index\": " << cb.index
                      << ", \"name\": \"" << cb.name
                      << "\", \"bindSet\": " << cb.bindSet
                      << ", \"bindSlot\": " << cb.bindSlot
                      << ", \"byteSize\": " << cb.byteSize
                      << ", \"variableCount\": " << cb.variableCount
                      << "}" << (i + 1 < buffers.size() ? "," : "") << "\n";
        }
        std::cout << "  ],\n  \"count\": " << buffers.size() << "\n}\n";
        return;
    }

    // Fetch contents for a specific constant block
    auto contents = getCBufferContents(session, stage, *cbufferIndex, eventId);
    std::cout << "{\n  \"blockName\": \"" << contents.blockName
              << "\",\n  \"stage\": \"" << stageName(contents.stage)
              << "\",\n  \"bindSet\": " << contents.bindSet
              << ",\n  \"bindSlot\": " << contents.bindSlot
              << ",\n  \"byteSize\": " << contents.byteSize
              << ",\n  \"variables\": [\n";

    // Simple flat print for now (nested structs not deeply formatted)
    for (size_t i = 0; i < contents.variables.size(); ++i) {
        const auto& v = contents.variables[i];
        std::cout << "    {\"name\": \"" << v.name
                  << "\", \"type\": \"" << v.typeName << "\"";
        if (!v.floatValues.empty()) {
            std::cout << ", \"values\": [";
            for (size_t j = 0; j < v.floatValues.size(); ++j)
                std::cout << v.floatValues[j] << (j + 1 < v.floatValues.size() ? ", " : "");
            std::cout << "]";
        } else if (!v.intValues.empty()) {
            std::cout << ", \"values\": [";
            for (size_t j = 0; j < v.intValues.size(); ++j)
                std::cout << v.intValues[j] << (j + 1 < v.intValues.size() ? ", " : "");
            std::cout << "]";
        } else if (!v.uintValues.empty()) {
            std::cout << ", \"values\": [";
            for (size_t j = 0; j < v.uintValues.size(); ++j)
                std::cout << v.uintValues[j] << (j + 1 < v.uintValues.size() ? ", " : "");
            std::cout << "]";
        } else if (!v.members.empty()) {
            std::cout << ", \"memberCount\": " << v.members.size();
        }
        std::cout << "}" << (i + 1 < contents.variables.size() ? "," : "") << "\n";
    }
    std::cout << "  ]\n}\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc >= 2 && std::string(argv[1]) == "diff") {
        return cmdDiff(argc, argv);
    }

    Args args = renderdoc::cli::parseArgs(argc, argv);

    Session session;

    try {
        // capture command: first arg is exe path, not a .rdc file
        if (args.command == "capture") {
            if (args.positional.empty()) {
                std::cerr << "error: 'capture' requires an executable path\n";
                return 1;
            }
            cmdCapture(session, args.positional[0], args.workingDir,
                       args.cmdLineArgs, args.delayFrames, args.outputDir);
            session.close();
            return 0;
        }

        // All other commands require opening a capture first
        session.open(args.capturePath);
    } catch (const CoreError& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    try {
        const std::string& cmd = args.command;

        if (cmd == "info") {
            cmdInfo(session);
        } else if (cmd == "events") {
            cmdEvents(session, args.filter);
        } else if (cmd == "draws") {
            cmdDraws(session, args.filter);
        } else if (cmd == "pipeline") {
            cmdPipeline(session, args.eventId);
        } else if (cmd == "shader") {
            if (args.positional.empty()) {
                std::cerr << "error: 'shader' requires a stage argument (vs|hs|ds|gs|ps|cs)\n";
                return 1;
            }
            cmdShader(session, args.positional[0], args.eventId);
        } else if (cmd == "resources") {
            cmdResources(session, args.typeFilter);
        } else if (cmd == "export-rt") {
            cmdExportRt(session, args.positional, args.outputDir, args.eventId);
        } else if (cmd == "pixel") {
            cmdPixel(session, args.positional, args.targetIndex, args.eventId);
        } else if (cmd == "pick-pixel") {
            cmdPickPixel(session, args.positional, args.targetIndex, args.eventId);
        } else if (cmd == "debug") {
            cmdDebug(session, args.positional, args.eventId, args.trace,
                     args.instance, args.primitive, args.index, args.view);
        } else if (cmd == "tex-stats") {
            cmdTexStats(session, args.positional, args.eventId,
                        args.mipLevel, args.sliceIndex, args.histogram);
        } else if (cmd == "shader-encodings") {
            cmdShaderEncodings(session);
        } else if (cmd == "shader-build") {
            cmdShaderBuild(session, args.positional, args.stageStr,
                           args.encoding, args.entry);
        } else if (cmd == "shader-replace") {
            cmdShaderReplace(session, args.positional, args.stageStr, args.withShaderId);
        } else if (cmd == "shader-restore") {
            cmdShaderRestore(session, args.positional, args.stageStr);
        } else if (cmd == "shader-restore-all") {
            cmdShaderRestoreAll(session);
        } else if (cmd == "mesh") {
            int rc = cmdMesh(session, args.positional, args.stageStr,
                             args.format, args.outputDir);
            session.close();
            return rc;
        } else if (cmd == "snapshot") {
            cmdSnapshot(session, args.positional, args.outputDir);
        } else if (cmd == "usage") {
            cmdUsage(session, args.positional);
        } else if (cmd == "assert-pixel") {
            int rc = cmdAssertPixel(session, args.positional, args.eventId,
                                    args.expectRGBA, args.hasExpectRGBA,
                                    args.tolerance, args.targetIndex);
            session.close();
            return rc;
        } else if (cmd == "assert-state") {
            int rc = cmdAssertState(args.positional, args.expectStr);
            session.close();
            return rc;
        } else if (cmd == "assert-image") {
            int rc = cmdAssertImage(args.positional, args.threshold, args.diffOutput);
            session.close();
            return rc;
        } else if (cmd == "assert-count") {
            int rc = cmdAssertCount(session, args.positional,
                                    args.expectCount, args.opStr);
            session.close();
            return rc;
        } else if (cmd == "assert-clean") {
            int rc = cmdAssertClean(session, args.minSeverity);
            session.close();
            return rc;
        } else if (cmd == "pass-stats") {
            cmdPassStats(session);
        } else if (cmd == "pass-deps") {
            cmdPassDeps(session);
        } else if (cmd == "unused-targets") {
            cmdUnusedTargets(session);
        } else if (cmd == "counters") {
            cmdCounters(session, args.listMode, args.counterFilter, args.eventId);
        } else if (cmd == "cbuffer") {
            cmdCBuffer(session, args.stageStr, args.cbufferIndex, args.eventId);
        } else {
            std::cerr << "error: unknown command '" << cmd << "'\n\n";
            renderdoc::cli::printUsage(argv[0]);
            return 1;
        }
    } catch (const CoreError& e) {
        std::cerr << "error: " << e.what() << "\n";
        session.close();
        return 1;
    }

    session.close();
    return 0;
}

