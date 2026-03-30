#include <gtest/gtest.h>
#include "mcp/tool_registry.h"
#include "core/session.h"

using namespace renderdoc::mcp;

// Helper: register a dummy tool with a given schema
static void registerDummy(ToolRegistry& reg, const std::string& name,
                          const nlohmann::json& schema)
{
    reg.registerTool({
        name, "dummy " + name, schema,
        [](renderdoc::core::Session&, const nlohmann::json& args) -> nlohmann::json {
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
    renderdoc::core::Session s;
    EXPECT_THROW(reg.callTool("nonexistent", s, {}), InvalidParamsError);
}

TEST(ToolRegistryTest, RequiredFieldMissing_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"path", {{"type", "string"}}}}},
        {"required", nlohmann::json::array({"path"})}
    });
    renderdoc::core::Session s;
    EXPECT_THROW(reg.callTool("t", s, {}), InvalidParamsError);
}

TEST(ToolRegistryTest, WrongType_String_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"name", {{"type", "string"}}}}}
    });
    renderdoc::core::Session s;
    EXPECT_THROW(reg.callTool("t", s, {{"name", 123}}), InvalidParamsError);
}

TEST(ToolRegistryTest, WrongType_Integer_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"count", {{"type", "integer"}}}}}
    });
    renderdoc::core::Session s;
    EXPECT_THROW(reg.callTool("t", s, {{"count", "abc"}}), InvalidParamsError);
}

TEST(ToolRegistryTest, WrongType_Boolean_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"flag", {{"type", "boolean"}}}}}
    });
    renderdoc::core::Session s;
    EXPECT_THROW(reg.callTool("t", s, {{"flag", "yes"}}), InvalidParamsError);
}

TEST(ToolRegistryTest, EnumValidation_InvalidValue_Throws)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"mode", {{"type", "string"}, {"enum", {"a", "b"}}}}}}
    });
    renderdoc::core::Session s;
    EXPECT_THROW(reg.callTool("t", s, {{"mode", "c"}}), InvalidParamsError);
}

TEST(ToolRegistryTest, EnumValidation_ValidValue_Passes)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"mode", {{"type", "string"}, {"enum", {"a", "b"}}}}}}
    });
    renderdoc::core::Session s;
    EXPECT_NO_THROW(reg.callTool("t", s, {{"mode", "a"}}));
}

TEST(ToolRegistryTest, OptionalField_Absent_NoError)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"opt", {{"type", "string"}}}}}
        // no "required" array
    });
    renderdoc::core::Session s;
    EXPECT_NO_THROW(reg.callTool("t", s, nlohmann::json::object()));
}

TEST(ToolRegistryTest, UnknownField_Ignored)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"known", {{"type", "string"}}}}}
    });
    renderdoc::core::Session s;
    EXPECT_NO_THROW(reg.callTool("t", s, {{"known", "v"}, {"extra", 42}}));
}
