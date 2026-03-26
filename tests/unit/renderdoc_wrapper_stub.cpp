// Link stub for RenderdocWrapper — provides empty-body implementations
// so test-unit can link without renderdoc.lib.
// These are never called in unit tests; they only satisfy the linker.
#include "renderdoc_wrapper.h"

RenderdocWrapper::~RenderdocWrapper() {}

void RenderdocWrapper::shutdown() {}

nlohmann::json RenderdocWrapper::openCapture(const std::string&) { return {}; }
nlohmann::json RenderdocWrapper::listEvents(const std::string&) { return {}; }
nlohmann::json RenderdocWrapper::gotoEvent(uint32_t) { return {}; }
nlohmann::json RenderdocWrapper::getPipelineState() { return {}; }
nlohmann::json RenderdocWrapper::exportRenderTarget(int) { return {}; }

std::string RenderdocWrapper::getExportDir() const { return {}; }
std::string RenderdocWrapper::generateOutputPath(uint32_t, int) const { return {}; }

void RenderdocWrapper::ensureReplayInitialized() {}
void RenderdocWrapper::closeCurrent() {}
