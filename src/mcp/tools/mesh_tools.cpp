#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/mesh.h"
#include <fstream>

namespace renderdoc::mcp::tools {

void registerMeshTools(ToolRegistry& registry) {

    registry.registerTool({
        "export_mesh",
        "Export post-transform vertex data from a draw call as OBJ or JSON. "
        "Decodes vertex positions and generates triangle faces from topology.",
        {{"type", "object"},
         {"properties", {
             {"eventId",    {{"type", "integer"}, {"description", "Draw call event ID"}}},
             {"stage",      {{"type", "string"}, {"enum", {"vs-out", "gs-out"}},
                             {"description", "Post-transform stage (default: vs-out)"}}},
             {"format",     {{"type", "string"}, {"enum", {"obj", "json"}},
                             {"description", "Output format (default: obj)"}}},
             {"outputPath", {{"type", "string"}, {"description", "File path to write (optional, returns inline if omitted)"}}}
         }},
         {"required", nlohmann::json::array({"eventId"})}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            uint32_t eventId = args["eventId"].get<uint32_t>();
            std::string stageStr = args.value("stage", std::string("vs-out"));
            core::MeshStage stage = (stageStr == "gs-out") ? core::MeshStage::GSOut : core::MeshStage::VSOut;
            std::string format = args.value("format", std::string("obj"));

            auto data = core::exportMesh(session, eventId, stage);

            if (args.contains("outputPath")) {
                std::string path = args["outputPath"].get<std::string>();
                std::ofstream f(path);
                if (format == "json") f << to_json(data).dump(2);
                else f << core::meshToObj(data);
                return {{"outputPath", path}, {"vertexCount", data.vertices.size()},
                        {"faceCount", data.faces.size()}};
            }

            if (format == "json") return to_json(data);
            return {{"obj", core::meshToObj(data)}, {"vertexCount", data.vertices.size()},
                    {"faceCount", data.faces.size()}};
        }
    });
}

} // namespace renderdoc::mcp::tools
