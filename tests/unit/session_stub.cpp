// Stub implementations so unit tests link without renderdoc.
#include "core/session.h"
#include "core/capture.h"
#include "mcp/serialization.h"

// Stub actionFlagsToString for unit tests — uses hardcoded bit values
// matching RenderDoc's ActionFlags enum so serialization tests pass.
namespace renderdoc::mcp {
std::string actionFlagsToString(core::ActionFlagBits flags) {
    std::string result;
    auto append = [&](uint32_t bit, const char* name) {
        if (flags & bit) {
            if (!result.empty()) result += "|";
            result += name;
        }
    };
    append(0x0001, "Clear");
    append(0x0002, "Drawcall");
    append(0x0004, "Dispatch");
    append(0x0008, "CmdList");
    append(0x0010, "SetMarker");
    append(0x0020, "PushMarker");
    append(0x0040, "PopMarker");
    append(0x0080, "Present");
    append(0x0100, "MultiAction");
    append(0x0200, "Copy");
    append(0x0400, "Resolve");
    append(0x0800, "GenMips");
    append(0x1000, "PassBoundary");
    append(0x10000, "Indexed");
    append(0x20000, "Instanced");
    append(0x40000, "Auto");
    return result.empty() ? "NoFlags" : result;
}
} // namespace renderdoc::mcp

namespace renderdoc::core {
Session::Session() = default;
Session::~Session() = default;
void Session::ensureReplayInitialized() {}
void Session::closeCurrent() {}
void Session::close() {}
CaptureInfo Session::open(const std::string&) { return {}; }
SessionStatus Session::status() const { return {}; }
bool Session::isOpen() const { return false; }
IReplayController* Session::controller() const { return nullptr; }
ICaptureFile* Session::captureFile() const { return nullptr; }
uint32_t Session::currentEventId() const { return 0; }
const std::string& Session::capturePath() const { static std::string e; return e; }
std::string Session::exportDir() const { return "."; }
void Session::setCurrentEventId(uint32_t) {}
CaptureResult captureFrame(Session&, const CaptureRequest&) {
    return CaptureResult{"stub.rdc", 0};
}
} // namespace renderdoc::core
