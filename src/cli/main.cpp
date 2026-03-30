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

#include "core/errors.h"
#include "core/events.h"
#include "core/export.h"
#include "core/info.h"
#include "core/pipeline.h"
#include "core/resources.h"
#include "core/session.h"
#include "core/shaders.h"
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
              << "  export-rt IDX -o DIR [-e EID]\n";
}

static Args parseArgs(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage(argv[0]);
        std::exit(1);
    }

    Args a;
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

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    Args args = parseArgs(argc, argv);

    Session session;

    try {
        session.open(args.capturePath);
    } catch (const CoreError& e) {
        std::cerr << "error: failed to open capture: " << e.what() << "\n";
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
