#include "mcp/tools/tools.h"
#include "mcp/tool_registry.h"
#include "mcp/serialization.h"
#include "core/session.h"
#include "core/assertions.h"
#include "core/pipeline.h"
#include "core/errors.h"
#include <sstream>

namespace renderdoc::mcp::tools {

// JSON path navigation helper — lives in MCP layer where nlohmann::json is available.
// Navigates a JSON object by dot-separated path with [N] array index support.
static nlohmann::json navigatePath(const nlohmann::json& root, const std::string& path) {
    nlohmann::json current = root;
    std::string segment;
    std::istringstream stream(path);

    while (std::getline(stream, segment, '.')) {
        auto bracket = segment.find('[');
        if (bracket != std::string::npos) {
            std::string field = segment.substr(0, bracket);
            auto closeBracket = segment.find(']', bracket);
            if (closeBracket == std::string::npos)
                throw core::CoreError(core::CoreError::Code::InvalidPath, "Unclosed bracket in: " + path);
            int index = 0;
            try {
                index = std::stoi(segment.substr(bracket + 1, closeBracket - bracket - 1));
            } catch (const std::exception&) {
                throw core::CoreError(core::CoreError::Code::InvalidPath,
                    "Invalid array index in path: " + segment);
            }
            if (!field.empty()) {
                if (!current.contains(field))
                    throw core::CoreError(core::CoreError::Code::InvalidPath, "Field not found: " + field);
                current = current[field];
            }
            if (!current.is_array() || index < 0 || index >= static_cast<int>(current.size()))
                throw core::CoreError(core::CoreError::Code::InvalidPath, "Invalid array index: " + std::to_string(index));
            current = current[index];
        } else {
            if (!current.contains(segment))
                throw core::CoreError(core::CoreError::Code::InvalidPath, "Field not found: " + segment);
            current = current[segment];
        }
    }
    return current;
}

// Normalize a JSON value to string for comparison (bools lowercase, etc.)
static std::string jsonValueToString(const nlohmann::json& value) {
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_number_integer()) return std::to_string(value.get<int64_t>());
    if (value.is_number_float()) return std::to_string(value.get<double>());
    if (value.is_string()) return value.get<std::string>();
    return value.dump();
}

void registerAssertionTools(ToolRegistry& registry) {

    registry.registerTool({
        "assert_pixel",
        "Assert that a pixel at (x,y) matches expected RGBA values within tolerance. "
        "Returns pass/fail with actual vs expected values.",
        {{"type", "object"},
         {"properties", {
             {"eventId",   {{"type", "integer"}, {"description", "Event ID"}}},
             {"x",         {{"type", "integer"}, {"description", "Pixel X coordinate"}}},
             {"y",         {{"type", "integer"}, {"description", "Pixel Y coordinate"}}},
             {"expected",  {{"type", "array"}, {"items", {{"type", "number"}}},
                            {"description", "[R, G, B, A] float values"}}},
             {"tolerance", {{"type", "number"}, {"description", "Per-channel tolerance (default 0.01)"}}},
             {"target",    {{"type", "integer"}, {"description", "Render target index (default 0)"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "x", "y", "expected"})}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            uint32_t eventId = args["eventId"].get<uint32_t>();
            uint32_t x = args["x"].get<uint32_t>();
            uint32_t y = args["y"].get<uint32_t>();
            auto expectedVec = args["expected"].get<std::vector<float>>();
            if (expectedVec.size() != 4)
                throw InvalidParamsError("'expected' must be an array of exactly 4 floats [R, G, B, A], got " + std::to_string(expectedVec.size()) + " elements");
            float expected[4] = {expectedVec[0], expectedVec[1], expectedVec[2], expectedVec[3]};
            float tolerance = args.value("tolerance", 0.01f);
            uint32_t target = args.value("target", 0u);
            auto result = core::assertPixel(session, eventId, x, y, expected, tolerance, target);
            return to_json(result);
        }
    });

    registry.registerTool({
        "assert_state",
        "Assert that a pipeline state field matches an expected value. "
        "Path uses dot notation matching the pipeline JSON output (e.g. vertexShader.entryPoint).",
        {{"type", "object"},
         {"properties", {
             {"eventId",  {{"type", "integer"}, {"description", "Event ID"}}},
             {"path",     {{"type", "string"}, {"description", "Dot-separated path (e.g. vertexShader.entryPoint)"}}},
             {"expected", {{"type", "string"}, {"description", "Expected value (string comparison)"}}}
         }},
         {"required", nlohmann::json::array({"eventId", "path", "expected"})}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            uint32_t eventId = args["eventId"].get<uint32_t>();
            auto path = args["path"].get<std::string>();
            auto expected = args["expected"].get<std::string>();

            // JSON navigation happens here in MCP layer, not in core
            auto pipeState = core::getPipelineState(session, eventId);
            nlohmann::json pipeJson = to_json(pipeState);
            nlohmann::json value = navigatePath(pipeJson, path);
            std::string actual = jsonValueToString(value);

            // Core just does the string comparison
            auto result = core::assertState(path, actual, expected);
            return to_json(result);
        }
    });

    registry.registerTool({
        "assert_image",
        "Compare two PNG images pixel-by-pixel. Returns pass/fail with diff statistics. "
        "Optionally writes a diff visualization PNG.",
        {{"type", "object"},
         {"properties", {
             {"expectedPath",  {{"type", "string"}, {"description", "Path to expected PNG"}}},
             {"actualPath",    {{"type", "string"}, {"description", "Path to actual PNG"}}},
             {"threshold",     {{"type", "number"}, {"description", "Max diff ratio % to pass (default 0.0)"}}},
             {"diffOutputPath",{{"type", "string"}, {"description", "Path to write diff visualization PNG"}}}
         }},
         {"required", nlohmann::json::array({"expectedPath", "actualPath"})}},
        [](mcp::ToolContext&, const nlohmann::json& args) -> nlohmann::json {
            auto expectedPath = args["expectedPath"].get<std::string>();
            auto actualPath = args["actualPath"].get<std::string>();
            double threshold = args.value("threshold", 0.0);
            auto diffOutput = args.value("diffOutputPath", std::string(""));
            auto result = core::assertImage(expectedPath, actualPath, threshold, diffOutput);
            return to_json(result);
        }
    });

    registry.registerTool({
        "assert_count",
        "Assert that a count of resources/events/draws matches expected value. "
        "Uses accurate counts bypassing default list limits.",
        {{"type", "object"},
         {"properties", {
             {"what",     {{"type", "string"}, {"enum", {"draws","events","textures","buffers","passes"}},
                           {"description", "What to count"}}},
             {"expected", {{"type", "integer"}, {"description", "Expected count"}}},
             {"op",       {{"type", "string"}, {"enum", {"eq","gt","lt","ge","le"}},
                           {"description", "Comparison operator (default: eq)"}}}
         }},
         {"required", nlohmann::json::array({"what", "expected"})}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            auto what = args["what"].get<std::string>();
            int64_t expected = args["expected"].get<int64_t>();
            auto op = args.value("op", std::string("eq"));
            auto result = core::assertCount(session, what, expected, op);
            return to_json(result);
        }
    });

    registry.registerTool({
        "assert_clean",
        "Assert that no debug/validation messages exist at or above the specified severity. "
        "Returns pass/fail with any matching messages.",
        {{"type", "object"},
         {"properties", {
             {"minSeverity", {{"type", "string"}, {"enum", {"high","medium","low","info"}},
                              {"description", "Minimum severity to fail on (default: high)"}}}
         }},
         {"required", nlohmann::json::array()}},
        [](mcp::ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
            auto& session = ctx.session;
            auto minSeverity = args.value("minSeverity", std::string("high"));
            auto result = core::assertClean(session, minSeverity);
            return to_json(result);
        }
    });
}

} // namespace renderdoc::mcp::tools
