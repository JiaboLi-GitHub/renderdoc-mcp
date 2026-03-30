#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/shaders.h"

namespace renderdoc::mcp::tools {

void registerShaderTools(ToolRegistry& registry) {

    // ── get_shader ────────────────────────────────────────────────────────────
    registry.registerTool({
        "get_shader",
        "Get shader disassembly or reflection data at an event for a given stage",
        {{"type", "object"},
         {"properties", {
             {"eventId", {{"type", "integer"}, {"description", "Event ID (uses current if omitted)"}}},
             {"stage",   {{"type", "string"},  {"enum", nlohmann::json::array({"vs","hs","ds","gs","ps","cs"})}}},
             {"mode",    {{"type", "string"},  {"enum", nlohmann::json::array({"disasm","reflect"})}, {"default", "disasm"}}}
         }},
         {"required", nlohmann::json::array({"stage"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            const std::string stageStr = args["stage"].get<std::string>();
            const std::string mode     = args.value("mode", std::string("disasm"));

            core::ShaderStage stage = parseShaderStage(stageStr);

            std::optional<uint32_t> eventId;
            if (args.contains("eventId"))
                eventId = args["eventId"].get<uint32_t>();

            if (mode == "reflect") {
                auto refl = core::getShaderReflection(session, stage, eventId);
                return to_json(refl);
            } else {
                // Default: disasm
                auto disasm = core::getShaderDisassembly(session, stage, eventId);
                return to_json(disasm);
            }
        }
    });

    // ── list_shaders ──────────────────────────────────────────────────────────
    registry.registerTool({
        "list_shaders",
        "List all unique shaders used in the capture with their stages and usage count",
        {{"type", "object"},
         {"properties", nlohmann::json::object()}},
        [](core::Session& session, const nlohmann::json& /*args*/) -> nlohmann::json {
            auto shaders = core::listShaders(session);
            nlohmann::json result;
            result["shaders"] = to_json_array(shaders);
            result["count"]   = shaders.size();
            return result;
        }
    });

    // ── search_shaders ────────────────────────────────────────────────────────
    registry.registerTool({
        "search_shaders",
        "Search shader disassembly text across all shaders for a pattern",
        {{"type", "object"},
         {"properties", {
             {"pattern", {{"type", "string"},  {"description", "Text pattern to search for (case-insensitive substring)"}}},
             {"stage",   {{"type", "string"},  {"enum", nlohmann::json::array({"vs","hs","ds","gs","ps","cs"})}, {"description", "Limit to specific stage"}}},
             {"limit",   {{"type", "integer"}, {"description", "Max results, default 50"}}}
         }},
         {"required", nlohmann::json::array({"pattern"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            const std::string pattern = args["pattern"].get<std::string>();
            uint32_t limit = (uint32_t)args.value("limit", 50);

            std::optional<core::ShaderStage> stageFilter;
            if (args.contains("stage")) {
                const std::string stageStr = args["stage"].get<std::string>();
                if (!stageStr.empty())
                    stageFilter = parseShaderStage(stageStr);
            }

            auto matches = core::searchShaders(session, pattern, stageFilter, limit);

            nlohmann::json result;
            result["matches"] = to_json_array(matches);
            result["count"]   = matches.size();
            result["pattern"] = pattern;
            return result;
        }
    });

}

} // namespace renderdoc::mcp::tools
