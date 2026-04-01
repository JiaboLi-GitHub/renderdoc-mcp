#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/debug.h"

namespace renderdoc::mcp::tools {

void registerDebugTools(ToolRegistry& registry) {

    registry.registerTool({
        "debug_pixel",
        "Debug the pixel/fragment shader at a specific pixel. "
        "Returns shader inputs, outputs, and optionally a full step-by-step execution trace. "
        "Variables preserve their original types (float/uint/int) and struct members.",
        {{"type", "object"},
         {"properties", {
             {"eventId",   {{"type", "integer"}, {"description", "Draw call event ID"}}},
             {"x",         {{"type", "integer"}, {"description", "Pixel X coordinate"}}},
             {"y",         {{"type", "integer"}, {"description", "Pixel Y coordinate"}}},
             {"mode",      {{"type", "string"}, {"enum", {"summary", "trace"}},
                            {"description", "summary=inputs/outputs only (default), trace=full execution"}}},
             {"primitive", {{"type", "integer"}, {"description", "Primitive ID (default: any)"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "x", "y"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            uint32_t eventId = args["eventId"].get<uint32_t>();
            uint32_t x = args["x"].get<uint32_t>();
            uint32_t y = args["y"].get<uint32_t>();
            bool fullTrace = (args.value("mode", std::string("summary")) == "trace");
            uint32_t primitive = args.value("primitive", 0xFFFFFFFFu);
            auto result = core::debugPixel(session, eventId, x, y, fullTrace, primitive);
            return to_json(result);
        }
    });

    registry.registerTool({
        "debug_vertex",
        "Debug the vertex shader for a specific vertex. "
        "Returns shader inputs, outputs, and optionally a full execution trace.",
        {{"type", "object"},
         {"properties", {
             {"eventId",  {{"type", "integer"}, {"description", "Draw call event ID"}}},
             {"vertexId", {{"type", "integer"}, {"description", "Vertex index to debug"}}},
             {"mode",     {{"type", "string"}, {"enum", {"summary", "trace"}},
                           {"description", "summary (default) or trace"}}},
             {"instance", {{"type", "integer"}, {"description", "Instance index, default 0"}}},
             {"index",    {{"type", "integer"}, {"description", "Raw index buffer value for indexed draws"}}},
             {"view",     {{"type", "integer"}, {"description", "Multiview view index, default 0"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "vertexId"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            uint32_t eventId  = args["eventId"].get<uint32_t>();
            uint32_t vertexId = args["vertexId"].get<uint32_t>();
            bool fullTrace = (args.value("mode", std::string("summary")) == "trace");
            uint32_t instance = args.value("instance", 0u);
            uint32_t index    = args.value("index", 0xFFFFFFFFu);
            uint32_t view     = args.value("view", 0u);
            auto result = core::debugVertex(session, eventId, vertexId, fullTrace, instance, index, view);
            return to_json(result);
        }
    });

    registry.registerTool({
        "debug_thread",
        "Debug a compute shader thread at a specific workgroup and thread coordinate. "
        "Returns shader inputs, outputs, and optionally a full execution trace.",
        {{"type", "object"},
         {"properties", {
             {"eventId", {{"type", "integer"}, {"description", "Dispatch event ID"}}},
             {"groupX",  {{"type", "integer"}, {"description", "Workgroup X"}}},
             {"groupY",  {{"type", "integer"}, {"description", "Workgroup Y"}}},
             {"groupZ",  {{"type", "integer"}, {"description", "Workgroup Z"}}},
             {"threadX", {{"type", "integer"}, {"description", "Thread X within workgroup"}}},
             {"threadY", {{"type", "integer"}, {"description", "Thread Y within workgroup"}}},
             {"threadZ", {{"type", "integer"}, {"description", "Thread Z within workgroup"}}},
             {"mode",    {{"type", "string"}, {"enum", {"summary", "trace"}},
                          {"description", "summary (default) or trace"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "groupX", "groupY", "groupZ",
                                             "threadX", "threadY", "threadZ"})}},
        [](core::Session& session, const nlohmann::json& args) -> nlohmann::json {
            uint32_t eventId = args["eventId"].get<uint32_t>();
            uint32_t gx = args["groupX"].get<uint32_t>();
            uint32_t gy = args["groupY"].get<uint32_t>();
            uint32_t gz = args["groupZ"].get<uint32_t>();
            uint32_t tx = args["threadX"].get<uint32_t>();
            uint32_t ty = args["threadY"].get<uint32_t>();
            uint32_t tz = args["threadZ"].get<uint32_t>();
            bool fullTrace = (args.value("mode", std::string("summary")) == "trace");
            auto result = core::debugThread(session, eventId, gx, gy, gz, tx, ty, tz, fullTrace);
            return to_json(result);
        }
    });
}

} // namespace renderdoc::mcp::tools
