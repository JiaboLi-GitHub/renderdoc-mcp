// renderdoc-cli — one-shot compound-command CLI
//
// Usage: renderdoc-cli <capture.rdc> <command> [args...]
//
// Commands:
//   info                          Print capture metadata
//   events [--filter TEXT]        List all events
//   draws  [--filter TEXT]        List draw calls
//   pipeline [-e EID]             Dump pipeline state at event
//   shader STAGE [-e EID]         Print shader disassembly (stage: vs|hs|ds|gs|ps|cs)
//   resources [--type TYPE]       List resources
//   export-rt IDX -o DIR [-e EID] Export render target to directory

#include "core/capture.h"
#include "core/debug.h"
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

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace renderdoc::core;

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

static std::optional<ShaderStage> parseStage(const std::string& s) {
    if (s == "vs" || s == "VS") return ShaderStage::Vertex;
    if (s == "hs" || s == "HS") return ShaderStage::Hull;
    if (s == "ds" || s == "DS") return ShaderStage::Domain;
    if (s == "gs" || s == "GS") return ShaderStage::Geometry;
    if (s == "ps" || s == "PS") return ShaderStage::Pixel;
    if (s == "cs" || s == "CS") return ShaderStage::Compute;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------

struct Args {
    std::string capturePath;
    std::string command;
    std::vector<std::string> positional; // extra positional args after command
    std::optional<uint32_t> eventId;
    std::string filter;
    std::string typeFilter;
    std::string outputDir;
    std::string workingDir;
    std::string cmdLineArgs;
    uint32_t delayFrames = 100;
    // Phase 1 additions
    uint32_t targetIndex = 0;
    uint32_t mipLevel = 0;
    uint32_t sliceIndex = 0;
    uint32_t instance = 0;
    uint32_t primitive = 0xFFFFFFFF;
    uint32_t index = 0xFFFFFFFF;
    uint32_t view = 0;
    bool trace = false;
    bool histogram = false;
};

static void printUsage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <capture.rdc> <command> [options]\n\n"
              << "Commands:\n"
              << "  info\n"
              << "  events [--filter TEXT]\n"
              << "  draws  [--filter TEXT]\n"
              << "  pipeline [-e EID]\n"
              << "  shader STAGE [-e EID]   (STAGE: vs|hs|ds|gs|ps|cs)\n"
              << "  resources [--type TYPE]\n"
              << "  export-rt IDX -o DIR [-e EID]\n"
              << "  capture EXE [-w DIR] [-a ARGS] [-d N] [-o PATH]\n"
              << "  pixel X Y [-e EID] [--target N]\n"
              << "  pick-pixel X Y [-e EID] [--target N]\n"
              << "  debug pixel X Y -e EID [--trace] [--primitive N]\n"
              << "  debug vertex VTX -e EID [--trace] [--instance N] [--index N] [--view N]\n"
              << "  debug thread GX GY GZ TX TY TZ -e EID [--trace]\n"
              << "  tex-stats RES_ID [-e EID] [--mip N] [--slice N] [--histogram]\n";
}

static Args parseArgs(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        std::exit(1);
    }

    Args a;

    // Special case: "capture" command doesn't take a .rdc as first arg
    if (std::string(argv[1]) == "capture") {
        a.command = "capture";
        int i = 2;
        while (i < argc) {
            std::string tok = argv[i];
            if ((tok == "-w" || tok == "--working-dir") && i + 1 < argc) {
                a.workingDir = argv[++i];
            } else if ((tok == "-a" || tok == "--args") && i + 1 < argc) {
                a.cmdLineArgs = argv[++i];
            } else if ((tok == "-d" || tok == "--delay-frames") && i + 1 < argc) {
                a.delayFrames = static_cast<uint32_t>(std::stoul(argv[++i]));
            } else if ((tok == "-o" || tok == "--output") && i + 1 < argc) {
                a.outputDir = argv[++i];
            } else {
                a.positional.push_back(tok);
            }
            ++i;
        }
        return a;
    }

    // Standard commands: <capture.rdc> <command> [options]
    if (argc < 3) {
        printUsage(argv[0]);
        std::exit(1);
    }

    a.capturePath = argv[1];
    a.command     = argv[2];

    int i = 3;
    while (i < argc) {
        std::string tok = argv[i];
        if ((tok == "-e" || tok == "--event") && i + 1 < argc) {
            a.eventId = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--filter" && i + 1 < argc) {
            a.filter = argv[++i];
        } else if (tok == "--type" && i + 1 < argc) {
            a.typeFilter = argv[++i];
        } else if (tok == "-o" && i + 1 < argc) {
            a.outputDir = argv[++i];
        } else if (tok == "--target" && i + 1 < argc) {
            a.targetIndex = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--mip" && i + 1 < argc) {
            a.mipLevel = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--slice" && i + 1 < argc) {
            a.sliceIndex = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--instance" && i + 1 < argc) {
            a.instance = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--primitive" && i + 1 < argc) {
            a.primitive = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--index" && i + 1 < argc) {
            a.index = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--view" && i + 1 < argc) {
            a.view = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (tok == "--trace") {
            a.trace = true;
        } else if (tok == "--histogram") {
            a.histogram = true;
        } else {
            a.positional.push_back(tok);
        }
        ++i;
    }

    return a;
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
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    Args args = parseArgs(argc, argv);

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
        } else {
            std::cerr << "error: unknown command '" << cmd << "'\n\n";
            printUsage(argv[0]);
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

