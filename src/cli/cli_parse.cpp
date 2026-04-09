#include "cli/cli_parse.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace renderdoc::cli {

std::optional<renderdoc::core::ShaderStage> parseStage(const std::string& s) {
    if (s == "vs" || s == "VS") return renderdoc::core::ShaderStage::Vertex;
    if (s == "hs" || s == "HS") return renderdoc::core::ShaderStage::Hull;
    if (s == "ds" || s == "DS") return renderdoc::core::ShaderStage::Domain;
    if (s == "gs" || s == "GS") return renderdoc::core::ShaderStage::Geometry;
    if (s == "ps" || s == "PS") return renderdoc::core::ShaderStage::Pixel;
    if (s == "cs" || s == "CS") return renderdoc::core::ShaderStage::Compute;
    return std::nullopt;
}

uint32_t parseUint32(const std::string& str, const std::string& flagName) {
    try {
        unsigned long val = std::stoul(str);
        return static_cast<uint32_t>(val);
    } catch (const std::invalid_argument&) {
        std::cerr << "error: invalid value '" << str << "' for " << flagName
                  << " (expected unsigned integer)\n";
        std::exit(1);
    } catch (const std::out_of_range&) {
        std::cerr << "error: value '" << str << "' for " << flagName
                  << " is out of range\n";
        std::exit(1);
    }
}

float parseFloat(const std::string& str, const std::string& flagName) {
    try {
        return std::stof(str);
    } catch (const std::invalid_argument&) {
        std::cerr << "error: invalid value '" << str << "' for " << flagName
                  << " (expected number)\n";
        std::exit(1);
    } catch (const std::out_of_range&) {
        std::cerr << "error: value '" << str << "' for " << flagName
                  << " is out of range\n";
        std::exit(1);
    }
}

uint64_t parseUint64(const std::string& str, const std::string& flagName) {
    try {
        return std::stoull(str);
    } catch (const std::invalid_argument&) {
        std::cerr << "error: invalid value '" << str << "' for " << flagName
                  << " (expected unsigned integer)\n";
        std::exit(1);
    } catch (const std::out_of_range&) {
        std::cerr << "error: value '" << str << "' for " << flagName
                  << " is out of range\n";
        std::exit(1);
    }
}

double parseDouble(const std::string& str, const std::string& flagName) {
    try {
        return std::stod(str);
    } catch (const std::invalid_argument&) {
        std::cerr << "error: invalid value '" << str << "' for " << flagName
                  << " (expected number)\n";
        std::exit(1);
    } catch (const std::out_of_range&) {
        std::cerr << "error: value '" << str << "' for " << flagName
                  << " is out of range\n";
        std::exit(1);
    }
}

void printUsage(const char* argv0) {
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
              << "  tex-stats RES_ID [-e EID] [--mip N] [--slice N] [--histogram]\n"
              << "  shader-encodings\n"
              << "  shader-build FILE --stage STAGE --encoding ENC [--entry NAME]\n"
              << "  shader-replace EID STAGE --with SHADER_ID\n"
              << "  shader-restore EID STAGE\n"
              << "  shader-restore-all\n"
              << "  mesh EID [--stage vs-out|gs-out] [--format obj|json] [-o FILE]\n"
              << "  snapshot EID -o DIR\n"
              << "  usage RES_ID\n"
              << "  assert-pixel EID X Y --expect R G B A [--tolerance T] [--target N]\n"
              << "  assert-state EID PATH --expect VALUE\n"
              << "  assert-image EXPECTED ACTUAL [--threshold T] [--diff-output PATH]\n"
              << "  assert-count WHAT --expect N [--op eq|gt|lt|ge|le]\n"
              << "  assert-clean [--min-severity high|medium|low|info]\n"
              << "  diff FILE_A FILE_B [--draws|--resources|--stats|--pipeline MARKER|--framebuffer]\n"
              << "  pass-stats\n"
              << "  pass-deps\n"
              << "  unused-targets\n";
}

Args parseArgs(int argc, char* argv[]) {
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
                a.delayFrames = parseUint32(argv[++i], "--delay-frames");
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
            a.eventId = parseUint32(argv[++i], "-e/--event");
        } else if (tok == "--filter" && i + 1 < argc) {
            a.filter = argv[++i];
        } else if (tok == "--type" && i + 1 < argc) {
            a.typeFilter = argv[++i];
        } else if (tok == "-o" && i + 1 < argc) {
            a.outputDir = argv[++i];
        } else if (tok == "--target" && i + 1 < argc) {
            a.targetIndex = parseUint32(argv[++i], "--target");
        } else if (tok == "--mip" && i + 1 < argc) {
            a.mipLevel = parseUint32(argv[++i], "--mip");
        } else if (tok == "--slice" && i + 1 < argc) {
            a.sliceIndex = parseUint32(argv[++i], "--slice");
        } else if (tok == "--instance" && i + 1 < argc) {
            a.instance = parseUint32(argv[++i], "--instance");
        } else if (tok == "--primitive" && i + 1 < argc) {
            a.primitive = parseUint32(argv[++i], "--primitive");
        } else if (tok == "--index" && i + 1 < argc) {
            a.index = parseUint32(argv[++i], "--index");
        } else if (tok == "--view" && i + 1 < argc) {
            a.view = parseUint32(argv[++i], "--view");
        } else if (tok == "--trace") {
            a.trace = true;
        } else if (tok == "--histogram") {
            a.histogram = true;
        } else if (tok == "--encoding" && i + 1 < argc) {
            a.encoding = argv[++i];
        } else if (tok == "--entry" && i + 1 < argc) {
            a.entry = argv[++i];
        } else if (tok == "--with" && i + 1 < argc) {
            a.withShaderId = parseUint64(argv[++i], "--with");
        } else if (tok == "--format" && i + 1 < argc) {
            a.format = argv[++i];
        } else if (tok == "--expect" && i + 1 < argc) {
            // Try to parse as 4 floats (for assert-pixel); fall back to string/int
            if (i + 4 < argc) {
                bool allNumeric = true;
                for (int k = 1; k <= 4; k++) {
                    std::string s = argv[i + k];
                    bool numeric = !s.empty() && (s[0] == '-' || s[0] == '.' ||
                                                  (s[0] >= '0' && s[0] <= '9'));
                    if (!numeric) { allNumeric = false; break; }
                }
                if (allNumeric) {
                    a.expectRGBA[0] = parseFloat(argv[++i], "--expect R");
                    a.expectRGBA[1] = parseFloat(argv[++i], "--expect G");
                    a.expectRGBA[2] = parseFloat(argv[++i], "--expect B");
                    a.expectRGBA[3] = parseFloat(argv[++i], "--expect A");
                    a.hasExpectRGBA = true;
                } else {
                    // Single value: string or int (int parsing is best-effort)
                    a.expectStr = argv[++i];
                    try { a.expectCount = std::stoi(a.expectStr); } catch (...) {}
                }
            } else {
                // Single value
                a.expectStr = argv[++i];
                try { a.expectCount = std::stoi(a.expectStr); } catch (...) {}
            }
        } else if (tok == "--tolerance" && i + 1 < argc) {
            a.tolerance = parseFloat(argv[++i], "--tolerance");
        } else if (tok == "--op" && i + 1 < argc) {
            a.opStr = argv[++i];
        } else if (tok == "--min-severity" && i + 1 < argc) {
            a.minSeverity = argv[++i];
        } else if (tok == "--diff-output" && i + 1 < argc) {
            a.diffOutput = argv[++i];
        } else if (tok == "--threshold" && i + 1 < argc) {
            a.threshold = parseDouble(argv[++i], "--threshold");
        } else if (tok == "--stage" && i + 1 < argc) {
            a.stageStr = argv[++i];
        } else {
            a.positional.push_back(tok);
        }
        ++i;
    }

    return a;
}

} // namespace renderdoc::cli
