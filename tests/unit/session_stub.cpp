// Stub implementations so unit tests link without renderdoc.
#include "core/session.h"
#include "core/capture.h"
#include "core/diff_session.h"
#include "core/remote_connection.h"
#include "core/errors.h"
#include "mcp/serialization.h"

// Provide IRemoteServer forward declaration for stub linking
struct IRemoteServer;

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

// RemoteConnection stubs
RemoteConnection::~RemoteConnection() {}
void RemoteConnection::connect(const std::string&) {}
void RemoteConnection::disconnect() {}
bool RemoteConnection::isConnected() const { return false; }
IRemoteServer* RemoteConnection::server() const { return nullptr; }
std::string RemoteConnection::copyCapture(const std::string&) { return ""; }
IReplayController* RemoteConnection::openCapture(const std::string&) { return nullptr; }
void RemoteConnection::closeCapture(IReplayController*) {}
void RemoteConnection::startPing() {}
void RemoteConnection::stopPing() {}

Session::Session() = default;
Session::~Session() = default;
void Session::ensureReplayInitialized() {}
void Session::closeCurrent() {}
void Session::close() {}
void Session::setRemoteUrl(const std::string&) {}
bool Session::isRemoteMode() const { return false; }
CaptureInfo Session::open(const std::string&) { return {}; }
SessionStatus Session::status() const { return {}; }
bool Session::isOpen() const { return false; }
IReplayController* Session::controller() const {
    throw CoreError(CoreError::Code::NoCaptureOpen, "No capture is currently open");
}
ICaptureFile* Session::captureFile() const { return nullptr; }
uint32_t Session::currentEventId() const { return 0; }
const std::string& Session::capturePath() const { static std::string e; return e; }
std::string Session::exportDir() const { return "."; }
void Session::setCurrentEventId(uint32_t) {}
CaptureResult captureFrame(Session&, const CaptureRequest&) {
    return CaptureResult{"stub.rdc", 0};
}
// Phase 1 stubs
PixelHistoryResult pixelHistory(const Session&, uint32_t, uint32_t, uint32_t,
                                std::optional<uint32_t>) { return {}; }
PickPixelResult pickPixel(const Session&, uint32_t, uint32_t, uint32_t,
                          std::optional<uint32_t>) { return {}; }
ShaderDebugResult debugPixel(const Session&, uint32_t, uint32_t, uint32_t,
                             bool, uint32_t) { return {}; }
ShaderDebugResult debugVertex(const Session&, uint32_t, uint32_t, bool,
                              uint32_t, uint32_t, uint32_t) { return {}; }
ShaderDebugResult debugThread(const Session&, uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t, uint32_t, uint32_t, bool) { return {}; }
TextureStats getTextureStats(const Session&, ResourceId, uint32_t, uint32_t, bool,
                             std::optional<uint32_t>) { return {}; }
// DiffSession stubs
DiffSession::DiffSession() = default;
DiffSession::~DiffSession() { close(); }
void DiffSession::setRemoteUrl(const std::string&) {}
bool DiffSession::isRemoteMode() const { return false; }
void DiffSession::close() {}
bool DiffSession::isOpen() const { return false; }
IReplayController* DiffSession::controllerA() const { return nullptr; }
IReplayController* DiffSession::controllerB() const { return nullptr; }
ICaptureFile* DiffSession::captureFileA() const { return nullptr; }
ICaptureFile* DiffSession::captureFileB() const { return nullptr; }
const std::string& DiffSession::pathA() const { static std::string s; return s; }
const std::string& DiffSession::pathB() const { static std::string s; return s; }
DiffSession::OpenResult DiffSession::open(const std::string&, const std::string&) {
    throw CoreError(CoreError::Code::DiffNotOpen, "stub");
}

// ShaderEditState accessor stubs — use member field to avoid cross-test interference
// from a shared static.
ShaderEditState& Session::shaderEditState() {
    return m_shaderEditState;
}
const ShaderEditState& Session::shaderEditState() const {
    return m_shaderEditState;
}

} // namespace renderdoc::core
