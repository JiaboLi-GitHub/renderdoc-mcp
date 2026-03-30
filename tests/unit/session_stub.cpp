// Stub implementations for core::Session so unit tests link without renderdoc.
#include "core/session.h"

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
} // namespace renderdoc::core
