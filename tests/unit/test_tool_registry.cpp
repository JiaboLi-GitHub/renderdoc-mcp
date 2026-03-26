#include <gtest/gtest.h>
#include "tool_registry.h"
#include "renderdoc_wrapper.h"

// Helper: register a dummy tool with a given schema
static void registerDummy(ToolRegistry& reg, const std::string& name,
                          const nlohmann::json& schema)
{
    reg.registerTool({
        name, "dummy " + name, schema,
        [](RenderdocWrapper&, const nlohmann::json& args) -> nlohmann::json {
            return {{"ok", true}};
        }
    });
}

TEST(ToolRegistryTest, HasTool_RegisteredTool_ReturnsTrue)
{
    ToolRegistry reg;
    registerDummy(reg, "foo", {{"type", "object"}, {"properties", nlohmann::json::object()}});
    EXPECT_TRUE(reg.hasTool("foo"));
}

TEST(ToolRegistryTest, HasTool_UnknownTool_ReturnsFalse)
{
    ToolRegistry reg;
    EXPECT_FALSE(reg.hasTool("nonexistent"));
}

TEST(ToolRegistryTest, CallTool_UnknownName_Throws)
{
    ToolRegistry reg;
    RenderdocWrapper w;
    EXPECT_THROW(reg.callTool("nonexistent", w, {}), InvalidParamsError);
}

TEST(ToolRegistryTest, RequiredFieldMissing_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"path", {{"type", "string"}}}}},
        {"required", nlohmann::json::array({"path"})}
    });
    RenderdocWrapper w;
    EXPECT_THROW(reg.callTool("t", w, {}), InvalidParamsError);
}

TEST(ToolRegistryTest, WrongType_String_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"name", {{"type", "string"}}}}}
    });
    RenderdocWrapper w;
    EXPECT_THROW(reg.callTool("t", w, {{"name", 123}}), InvalidParamsError);
}

TEST(ToolRegistryTest, WrongType_Integer_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"count", {{"type", "integer"}}}}}
    });
    RenderdocWrapper w;
    EXPECT_THROW(reg.callTool("t", w, {{"count", "abc"}}), InvalidParamsError);
}

TEST(ToolRegistryTest, WrongType_Boolean_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"flag", {{"type", "boolean"}}}}}
    });
    RenderdocWrapper w;
    EXPECT_THROW(reg.callTool("t", w, {{"flag", "yes"}}), InvalidParamsError);
}

TEST(ToolRegistryTest, EnumValidation_InvalidValue_Throws)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"mode", {{"type", "string"}, {"enum", {"a", "b"}}}}}}
    });
    RenderdocWrapper w;
    EXPECT_THROW(reg.callTool("t", w, {{"mode", "c"}}), InvalidParamsError);
}

TEST(ToolRegistryTest, EnumValidation_ValidValue_Passes)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"mode", {{"type", "string"}, {"enum", {"a", "b"}}}}}}
    });
    RenderdocWrapper w;
    EXPECT_NO_THROW(reg.callTool("t", w, {{"mode", "a"}}));
}

TEST(ToolRegistryTest, OptionalField_Absent_NoError)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"opt", {{"type", "string"}}}}}
        // no "required" array
    });
    RenderdocWrapper w;
    EXPECT_NO_THROW(reg.callTool("t", w, nlohmann::json::object()));
}

TEST(ToolRegistryTest, UnknownField_Ignored)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"known", {{"type", "string"}}}}}
    });
    RenderdocWrapper w;
    EXPECT_NO_THROW(reg.callTool("t", w, {{"known", "v"}, {"extra", 42}}));
}
