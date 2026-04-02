#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/shader_edit.h"

namespace renderdoc::mcp::tools {

void registerShaderEditTools(ToolRegistry& registry) {

    registry.registerTool({
        "shader_encodings",
        "List shader compilation encodings supported by the current capture. "
        "Call this before shader_build to determine valid encoding values.",
        {{"type", "object"}, {"properties", nlohmann::json::object()},
         {"required", nlohmann::json::array()}},
        [](mcp::ToolContext& ctx, const nlohmann::json&) -> nlohmann::json {
            auto& session = ctx.session;
            auto encodings = core::getShaderEncodings(session);
            return {{"encodings", encodings}};
        }
    });

    registry.registerTool({
        "shader_build",
        "Compile shader source code. Returns a shaderId for use with shader_replace. "
        "Encoding must be one of the values from shader_encodings.",
        {{"type", "object"},
         {"properties", {
             {"source",   {{"type", "string"}, {"description", "Shader source code"}}},
             {"stage",    {{"type", "string"}, {"enum", {"vs","hs","ds","gs","ps","cs"}},
                           {"description", "Shader stage"}}},
             {"entry",    {{"type", "string"}, {"description", "Entry point name (default: main)"}}},
             {"encoding", {{"type", "string"}, {"description", "Shader encoding (from shader_encodings)"}}}
         }},
         {"required", nlohmann::json::array({"source", "stage", "encoding"})}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            auto source = args["source"].get<std::string>();
            auto stage = parseShaderStage(args["stage"].get<std::string>());
            auto entry = args.value("entry", std::string("main"));
            auto encoding = args["encoding"].get<std::string>();
            auto result = core::buildShader(session, source, stage, entry, encoding);
            return to_json(result);
        }
    });

    registry.registerTool({
        "shader_replace",
        "Replace a shader at the given event/stage with a previously built shader. "
        "Affects ALL draws using the same shader globally.",
        {{"type", "object"},
         {"properties", {
             {"eventId",  {{"type", "integer"}, {"description", "Event ID to locate shader"}}},
             {"stage",    {{"type", "string"}, {"enum", {"vs","hs","ds","gs","ps","cs"}},
                           {"description", "Shader stage"}}},
             {"shaderId", {{"type", "integer"}, {"description", "Built shader ID from shader_build"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "stage", "shaderId"})}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            uint32_t eventId = args["eventId"].get<uint32_t>();
            auto stage = parseShaderStage(args["stage"].get<std::string>());
            uint64_t shaderId = args["shaderId"].get<uint64_t>();
            uint64_t originalId = core::replaceShader(session, eventId, stage, shaderId);
            return {{"originalId", originalId},
                    {"message", "Replacement active. Affects all draws using this shader."}};
        }
    });

    registry.registerTool({
        "shader_restore",
        "Restore a single shader to its original version.",
        {{"type", "object"},
         {"properties", {
             {"eventId", {{"type", "integer"}, {"description", "Event ID"}}},
             {"stage",   {{"type", "string"}, {"enum", {"vs","hs","ds","gs","ps","cs"}},
                          {"description", "Shader stage"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "stage"})}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            uint32_t eventId = args["eventId"].get<uint32_t>();
            auto stage = parseShaderStage(args["stage"].get<std::string>());
            core::restoreShader(session, eventId, stage);
            return {{"restored", true}};
        }
    });

    registry.registerTool({
        "shader_restore_all",
        "Restore all replaced shaders and free all built shader resources.",
        {{"type", "object"}, {"properties", nlohmann::json::object()},
         {"required", nlohmann::json::array()}},
        [](mcp::ToolContext& ctx, const nlohmann::json&) -> nlohmann::json {
            auto& session = ctx.session;
            auto [restored, freed] = core::restoreAllShaders(session);
            return {{"restoredCount", restored}, {"freedCount", freed}};
        }
    });
}

} // namespace renderdoc::mcp::tools
