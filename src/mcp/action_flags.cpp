#include "mcp/serialization.h"
#include <renderdoc_replay.h>

namespace renderdoc::mcp {

std::string actionFlagsToString(core::ActionFlagBits flags) {
    // MUST match the exact flag names and check order from the original
    // actionFlagsToString to preserve the MCP wire contract.
    std::string result;
    auto af = static_cast<ActionFlags>(flags);
    auto append = [&](ActionFlags bit, const char* name) {
        if (af & bit) {
            if (!result.empty()) result += "|";
            result += name;
        }
    };
    append(ActionFlags::Clear, "Clear");
    append(ActionFlags::Drawcall, "Drawcall");
    append(ActionFlags::Dispatch, "Dispatch");
    append(ActionFlags::MeshDispatch, "MeshDispatch");
    append(ActionFlags::CmdList, "CmdList");
    append(ActionFlags::SetMarker, "SetMarker");
    append(ActionFlags::PushMarker, "PushMarker");
    append(ActionFlags::PopMarker, "PopMarker");
    append(ActionFlags::Present, "Present");
    append(ActionFlags::MultiAction, "MultiAction");
    append(ActionFlags::Copy, "Copy");
    append(ActionFlags::Resolve, "Resolve");
    append(ActionFlags::GenMips, "GenMips");
    append(ActionFlags::PassBoundary, "PassBoundary");
    append(ActionFlags::DispatchRay, "DispatchRay");
    append(ActionFlags::BuildAccStruct, "BuildAccStruct");
    append(ActionFlags::Indexed, "Indexed");
    append(ActionFlags::Instanced, "Instanced");
    append(ActionFlags::Indirect, "Indirect");
    return result.empty() ? "NoFlags" : result;
}

} // namespace renderdoc::mcp
