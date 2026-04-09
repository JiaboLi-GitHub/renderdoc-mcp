#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/cbuffer.h"
#include "core/session.h"

namespace renderdoc::mcp::tools {

void registerCBufferTools(ToolRegistry& registry) {
    // --- list_cbuffers ---
    registry.registerTool({
        "list_cbuffers",
        "List all constant buffer (uniform buffer) blocks bound to a shader stage. "
        "Returns metadata for each block: name, bind set/slot, byte size, and variable count. "
        "Use the returned index with get_cbuffer_contents to read actual values.",
        {{"type", "object"},
         {"properties", {
             {"stage", {{"type", "string"},
                        {"enum", {"vs", "hs", "ds", "gs", "ps", "cs"}},
                        {"description", "Shader stage to query."}}},
             {"eventId", {{"type", "integer"},
                          {"description", "Event ID to query at. Defaults to current event."}}}
         }},
         {"required", nlohmann::json::array({"stage"})}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            core::ShaderStage stage = parseShaderStage(args["stage"].get<std::string>());
            std::optional<uint32_t> eventId;
            if (args.contains("eventId"))
                eventId = args["eventId"].get<uint32_t>();

            auto buffers = core::listCBuffers(ctx.session, stage, eventId);
            nlohmann::json result;
            result["stage"] = shaderStageToString(stage);
            result["cbuffers"] = to_json_array(buffers);
            result["count"] = buffers.size();
            return result;
        }
    });

    // --- get_cbuffer_contents ---
    registry.registerTool({
        "get_cbuffer_contents",
        "Read the contents of a constant buffer (uniform buffer) block at a specific shader stage. "
        "Returns all variable names, types, and current values (floats, ints, matrices, structs). "
        "Essential for understanding shader parameters like transform matrices, material properties, "
        "light positions, and other uniform data that affects rendering.",
        {{"type", "object"},
         {"properties", {
             {"stage", {{"type", "string"},
                        {"enum", {"vs", "hs", "ds", "gs", "ps", "cs"}},
                        {"description", "Shader stage to query."}}},
             {"index", {{"type", "integer"},
                        {"description", "Constant block index (from list_cbuffers)."}}},
             {"eventId", {{"type", "integer"},
                          {"description", "Event ID to query at. Defaults to current event."}}}
         }},
         {"required", nlohmann::json::array({"stage", "index"})}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            core::ShaderStage stage = parseShaderStage(args["stage"].get<std::string>());
            uint32_t index = args["index"].get<uint32_t>();
            std::optional<uint32_t> eventId;
            if (args.contains("eventId"))
                eventId = args["eventId"].get<uint32_t>();

            auto contents = core::getCBufferContents(ctx.session, stage, index, eventId);
            return to_json(contents);
        }
    });
}

} // namespace renderdoc::mcp::tools
