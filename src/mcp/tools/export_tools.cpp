#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/export.h"

namespace renderdoc::mcp::tools {

void registerExportTools(ToolRegistry& registry) {

    // ── export_render_target ──────────────────────────────────────────────────
    registry.registerTool({
        "export_render_target",
        "Export the current event's render target as a PNG file. "
        "The file is saved to an auto-generated path in the capture's directory. "
        "Call goto_event first to select which event to export.",
        {{"type", "object"},
         {"properties", {
             {"index", {{"type", "integer"},
                        {"description", "Render target index (0-7), defaults to 0"},
                        {"default", 0}}}
         }}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            int index = args.value("index", 0);
            std::string dir = session.exportDir();
            auto result = core::exportRenderTarget(session, index, dir);
            return to_json(result);
        }
    });

    // ── export_texture ────────────────────────────────────────────────────────
    registry.registerTool({
        "export_texture",
        "Export a texture resource as a PNG image by resource ID.",
        {{"type", "object"},
         {"properties", {
             {"resourceId", {{"type", "string"},
                             {"description", "Resource ID (e.g. ResourceId::123)"}}},
             {"mip",        {{"type", "integer"},
                             {"description", "Mip level, default 0"}}},
             {"layer",      {{"type", "integer"},
                             {"description", "Array layer, default 0"}}}
         }},
         {"required", nlohmann::json::array({"resourceId"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            std::string idStr = args["resourceId"].get<std::string>();
            core::ResourceId id = parseResourceId(idStr);
            uint32_t mip   = args.value("mip",   0u);
            uint32_t layer = args.value("layer", 0u);
            std::string dir = session.exportDir();
            auto result = core::exportTexture(session, id, dir, mip, layer);
            return to_json(result);
        }
    });

    // ── export_buffer ─────────────────────────────────────────────────────────
    registry.registerTool({
        "export_buffer",
        "Export buffer data to a binary file.",
        {{"type", "object"},
         {"properties", {
             {"resourceId", {{"type", "string"},
                             {"description", "Resource ID"}}},
             {"offset",     {{"type", "integer"},
                             {"description", "Byte offset, default 0"}}},
             {"size",       {{"type", "integer"},
                             {"description", "Byte count (0 = all), default 0"}}}
         }},
         {"required", nlohmann::json::array({"resourceId"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            std::string idStr = args["resourceId"].get<std::string>();
            core::ResourceId id = parseResourceId(idStr);
            uint64_t offset = args.value("offset", uint64_t(0));
            uint64_t size   = args.value("size",   uint64_t(0));
            std::string dir = session.exportDir();
            auto result = core::exportBuffer(session, id, dir, offset, size);
            return to_json(result);
        }
    });

}

} // namespace renderdoc::mcp::tools
