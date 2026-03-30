// Link stub for RenderdocWrapper — provides empty-body implementations
// so test-unit can link without renderdoc.lib.
// These are never called in unit tests; they only satisfy the linker.
#include "renderdoc_wrapper.h"
#include "mcp/serialization.h"

// Stub for actionFlagsToString — defined in renderdoc-mcp-lib (needs renderdoc
// headers) but declared in serialization.h which is compiled into proto.
// This stub uses raw bitmask values that match action_flags.cpp so that
// test_serialization.cpp's EventInfoSerialization test passes without renderdoc.
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
    append(0x0008, "MeshDispatch");
    append(0x0010, "CmdList");
    append(0x0020, "SetMarker");
    append(0x0040, "PushMarker");
    append(0x0080, "PopMarker");
    append(0x0100, "Present");
    append(0x0200, "MultiAction");
    append(0x0400, "Copy");
    append(0x0800, "Resolve");
    append(0x1000, "GenMips");
    append(0x2000, "PassBoundary");
    append(0x4000, "DispatchRay");
    append(0x8000, "BuildAccStruct");
    append(0x10000, "Indexed");
    append(0x20000, "Instanced");
    append(0x40000, "Indirect");
    return result.empty() ? "NoFlags" : result;
}
} // namespace renderdoc::mcp

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
