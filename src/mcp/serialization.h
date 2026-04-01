#pragma once

#include "core/types.h"
#include <nlohmann/json.hpp>
#include <string>

namespace renderdoc::mcp {

// ResourceId canonical string format: "ResourceId::123"
std::string resourceIdToString(core::ResourceId id);
core::ResourceId parseResourceId(const std::string& str);

// ActionFlags bitmask -> pipe-separated string "Drawcall|Indexed|..."
// NOTE: This function lives in renderdoc-mcp-lib (not proto) because it
// needs the RenderDoc ActionFlags enum to cast bits back to symbolic names.
// It is declared here but defined in a separate TU compiled into mcp-lib.
std::string actionFlagsToString(core::ActionFlagBits flags);

// GraphicsApi enum -> string
std::string graphicsApiToString(core::GraphicsApi api);

// ShaderStage enum -> short string ("vs", "ps", etc.)
std::string shaderStageToString(core::ShaderStage stage);
core::ShaderStage parseShaderStage(const std::string& str);

// Struct -> JSON serializers
nlohmann::json to_json(const core::CaptureInfo& info);
nlohmann::json to_json(const core::SessionStatus& status);
nlohmann::json to_json(const core::EventInfo& event);
nlohmann::json to_json(const core::PipelineState& state);
nlohmann::json to_json(const core::StageBindings& bindings);
nlohmann::json to_json(const core::ResourceInfo& res);
nlohmann::json to_json(const core::PassInfo& pass);
nlohmann::json to_json(const core::DebugMessage& msg);
nlohmann::json to_json(const core::CaptureStats& stats);
nlohmann::json to_json(const core::ShaderReflection& refl);
nlohmann::json to_json(const core::ShaderDisassembly& disasm);
nlohmann::json to_json(const core::ShaderUsageInfo& info);
nlohmann::json to_json(const core::ShaderSearchMatch& match);
nlohmann::json to_json(const core::ExportResult& result);
nlohmann::json to_json(const core::CaptureResult& result);
nlohmann::json to_json(const core::BoundResource& binding);
nlohmann::json to_json(const core::RenderTargetInfo& rt);
nlohmann::json to_json(const core::PixelValue& val);
nlohmann::json to_json(const core::PixelModification& mod);
nlohmann::json to_json(const core::PixelHistoryResult& result);
nlohmann::json to_json(const core::PickPixelResult& result);
nlohmann::json to_json(const core::DebugVariable& var);
nlohmann::json to_json(const core::DebugVariableChange& change);
nlohmann::json to_json(const core::DebugStep& step);
nlohmann::json to_json(const core::ShaderDebugResult& result);
nlohmann::json to_json(const core::TextureStats& stats);

template<typename T>
nlohmann::json to_json_array(const std::vector<T>& vec) {
    auto arr = nlohmann::json::array();
    for (const auto& item : vec) arr.push_back(to_json(item));
    return arr;
}

} // namespace renderdoc::mcp
