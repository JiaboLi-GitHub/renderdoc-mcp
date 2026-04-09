#include <gtest/gtest.h>
#include "mcp/tool_registry.h"
#include "core/session.h"
#include "core/diff_session.h"

using namespace renderdoc::mcp;

// Helper: register a dummy tool with a given schema
static void registerDummy(ToolRegistry& reg, const std::string& name,
                          const nlohmann::json& schema)
{
    reg.registerTool({
        name, "dummy " + name, schema,
        [](renderdoc::mcp::ToolContext&, const nlohmann::json& args) -> nlohmann::json {
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
    renderdoc::core::DiffSession ds;
    ToolContext ctx{s, ds};
    EXPECT_THROW(reg.callTool("nonexistent", ctx, {}), InvalidParamsError);
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
    renderdoc::core::DiffSession ds;
    ToolContext ctx{s, ds};
    EXPECT_THROW(reg.callTool("t", ctx, {}), InvalidParamsError);
}

TEST(ToolRegistryTest, WrongType_String_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"name", {{"type", "string"}}}}}
    });
    renderdoc::core::Session s;
    renderdoc::core::DiffSession ds;
    ToolContext ctx{s, ds};
    EXPECT_THROW(reg.callTool("t", ctx, {{"name", 123}}), InvalidParamsError);
}

TEST(ToolRegistryTest, WrongType_Integer_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"count", {{"type", "integer"}}}}}
    });
    renderdoc::core::Session s;
    renderdoc::core::DiffSession ds;
    ToolContext ctx{s, ds};
    EXPECT_THROW(reg.callTool("t", ctx, {{"count", "abc"}}), InvalidParamsError);
}

TEST(ToolRegistryTest, WrongType_Boolean_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"flag", {{"type", "boolean"}}}}}
    });
    renderdoc::core::Session s;
    renderdoc::core::DiffSession ds;
    ToolContext ctx{s, ds};
    EXPECT_THROW(reg.callTool("t", ctx, {{"flag", "yes"}}), InvalidParamsError);
}

TEST(ToolRegistryTest, EnumValidation_InvalidValue_Throws)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"mode", {{"type", "string"}, {"enum", {"a", "b"}}}}}}
    });
    renderdoc::core::Session s;
    renderdoc::core::DiffSession ds;
    ToolContext ctx{s, ds};
    EXPECT_THROW(reg.callTool("t", ctx, {{"mode", "c"}}), InvalidParamsError);
}

TEST(ToolRegistryTest, EnumValidation_ValidValue_Passes)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"mode", {{"type", "string"}, {"enum", {"a", "b"}}}}}}
    });
    renderdoc::core::Session s;
    renderdoc::core::DiffSession ds;
    ToolContext ctx{s, ds};
    EXPECT_NO_THROW(reg.callTool("t", ctx, {{"mode", "a"}}));
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
    renderdoc::core::DiffSession ds;
    ToolContext ctx{s, ds};
    EXPECT_NO_THROW(reg.callTool("t", ctx, nlohmann::json::object()));
}

TEST(ToolRegistryTest, UnknownField_ThrowsInvalidParams)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"known", {{"type", "string"}}}}}
    });
    renderdoc::core::Session s;
    renderdoc::core::DiffSession ds;
    ToolContext ctx{s, ds};
    EXPECT_THROW(reg.callTool("t", ctx, {{"known", "v"}, {"extra", 42}}), InvalidParamsError);
}

TEST(ToolRegistryTest, DuplicateToolName_Throws)
{
    ToolRegistry reg;
    registerDummy(reg, "dup", {{"type", "object"}, {"properties", nlohmann::json::object()}});
    EXPECT_THROW(
        registerDummy(reg, "dup", {{"type", "object"}, {"properties", nlohmann::json::object()}}),
        std::logic_error);
}

TEST(ToolRegistryTest, MinimumValidation_BelowMin_Throws)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"val", {{"type", "integer"}, {"minimum", 0}}}}}
    });
    renderdoc::core::Session s;
    renderdoc::core::DiffSession ds;
    ToolContext ctx{s, ds};
    EXPECT_THROW(reg.callTool("t", ctx, {{"val", -1}}), InvalidParamsError);
}

TEST(ToolRegistryTest, MaximumValidation_AboveMax_Throws)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"val", {{"type", "integer"}, {"maximum", 10}}}}}
    });
    renderdoc::core::Session s;
    renderdoc::core::DiffSession ds;
    ToolContext ctx{s, ds};
    EXPECT_THROW(reg.callTool("t", ctx, {{"val", 11}}), InvalidParamsError);
}

TEST(ToolRegistryTest, MinMaxValidation_InRange_Passes)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"val", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10}}}}}
    });
    renderdoc::core::Session s;
    renderdoc::core::DiffSession ds;
    ToolContext ctx{s, ds};
    EXPECT_NO_THROW(reg.callTool("t", ctx, {{"val", 5}}));
}

TEST(ToolRegistryTest, MinLengthValidation_TooShort_Throws)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"name", {{"type", "string"}, {"minLength", 3}}}}}
    });
    renderdoc::core::Session s;
    renderdoc::core::DiffSession ds;
    ToolContext ctx{s, ds};
    EXPECT_THROW(reg.callTool("t", ctx, {{"name", "ab"}}), InvalidParamsError);
}

TEST(ToolRegistryTest, MaxLengthValidation_TooLong_Throws)
{
    ToolRegistry reg;
    registerDummy(reg, "t", {
        {"type", "object"},
        {"properties", {{"name", {{"type", "string"}, {"maxLength", 5}}}}}
    });
    renderdoc::core::Session s;
    renderdoc::core::DiffSession ds;
    ToolContext ctx{s, ds};
    EXPECT_THROW(reg.callTool("t", ctx, {{"name", "toolong"}}), InvalidParamsError);
}
