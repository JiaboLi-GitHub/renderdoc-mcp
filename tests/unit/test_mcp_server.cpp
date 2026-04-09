#include <gtest/gtest.h>
#include "mcp/mcp_server.h"
#include "mcp/tool_registry.h"
#include "core/session.h"
#include "core/diff_session.h"

using json = nlohmann::json;
using namespace renderdoc::mcp;

class McpServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_registry.registerTool({
            "echo_tool", "echoes input",
            {{"type", "object"}, {"properties", {{"msg", {{"type", "string"}}}}},
             {"required", json::array({"msg"})}},
            [](ToolContext&, const json& args) -> json {
                return {{"echo", args["msg"]}};
            }
        });
        m_registry.registerTool({
            "fail_tool", "always fails",
            {{"type", "object"}, {"properties", json::object()}},
            [](ToolContext&, const json&) -> json {
                throw std::runtime_error("deliberate failure");
            }
        });
        m_registry.registerTool({
            "invalid_tool", "throws InvalidParamsError",
            {{"type", "object"}, {"properties", json::object()}},
            [](ToolContext&, const json&) -> json {
                throw InvalidParamsError("bad param from handler");
            }
        });

        m_server = std::make_unique<McpServer>(m_session, m_diffSession, m_registry);
    }

    json makeRequest(const std::string& method, const json& params = json::object(), int id = 1) {
        json req;
        req["jsonrpc"] = "2.0";
        req["id"] = id;
        req["method"] = method;
        if(!params.is_null())
            req["params"] = params;
        return req;
    }

    void doInitialize() {
        m_server->handleMessage(makeRequest("initialize"));
        json notif;
        notif["jsonrpc"] = "2.0";
        notif["method"] = "notifications/initialized";
        m_server->handleMessage(notif);
    }

    renderdoc::core::Session m_session;
    renderdoc::core::DiffSession m_diffSession;
    ToolRegistry m_registry;
    std::unique_ptr<McpServer> m_server;
};

TEST_F(McpServerTest, Initialize_ReturnsServerInfo)
{
    auto resp = m_server->handleMessage(makeRequest("initialize"));
    ASSERT_TRUE(resp.contains("result"));
    EXPECT_EQ(resp["result"]["serverInfo"]["name"], "renderdoc-mcp");
    EXPECT_EQ(resp["result"]["protocolVersion"], "2025-03-26");
}

TEST_F(McpServerTest, Initialize_HasToolsCapability)
{
    auto resp = m_server->handleMessage(makeRequest("initialize"));
    EXPECT_TRUE(resp["result"]["capabilities"].contains("tools"));
}

TEST_F(McpServerTest, ToolsList_ReturnsRegisteredTools)
{
    doInitialize();
    auto resp = m_server->handleMessage(makeRequest("tools/list"));
    auto tools = resp["result"]["tools"];
    EXPECT_EQ(tools.size(), 3u);
}

TEST_F(McpServerTest, ToolsList_EachHasRequiredFields)
{
    doInitialize();
    auto resp = m_server->handleMessage(makeRequest("tools/list"));
    for(const auto& tool : resp["result"]["tools"]) {
        EXPECT_TRUE(tool.contains("name"));
        EXPECT_TRUE(tool.contains("description"));
        EXPECT_TRUE(tool.contains("inputSchema"));
    }
}

TEST_F(McpServerTest, ToolsCall_UnknownTool_ReturnsError)
{
    doInitialize();
    auto resp = m_server->handleMessage(makeRequest("tools/call",
        {{"name", "nonexistent"}, {"arguments", json::object()}}));
    ASSERT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32602);
}

TEST_F(McpServerTest, ToolsCall_ValidTool_ReturnsHandlerResult)
{
    doInitialize();
    auto resp = m_server->handleMessage(makeRequest("tools/call",
        {{"name", "echo_tool"}, {"arguments", {{"msg", "hello"}}}}));
    ASSERT_TRUE(resp.contains("result"));
    EXPECT_FALSE(resp["result"].value("isError", false));
    auto text = resp["result"]["content"][0]["text"].get<std::string>();
    auto parsed = json::parse(text);
    EXPECT_EQ(parsed["echo"], "hello");
}

TEST_F(McpServerTest, ToolsCall_HandlerThrowsRuntime_ReturnsIsError)
{
    doInitialize();
    auto resp = m_server->handleMessage(makeRequest("tools/call",
        {{"name", "fail_tool"}, {"arguments", json::object()}}));
    ASSERT_TRUE(resp.contains("result"));
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
}

TEST_F(McpServerTest, ToolsCall_HandlerThrowsInvalidParams_Returns32602)
{
    doInitialize();
    auto resp = m_server->handleMessage(makeRequest("tools/call",
        {{"name", "invalid_tool"}, {"arguments", json::object()}}));
    ASSERT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32602);
}

TEST_F(McpServerTest, UnknownMethod_ReturnsMethodNotFound)
{
    auto resp = m_server->handleMessage(makeRequest("unknown/method"));
    ASSERT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32601);
}

TEST_F(McpServerTest, InvalidParams_MissingToolName_Returns32602)
{
    doInitialize();
    auto resp = m_server->handleMessage(makeRequest("tools/call",
        {{"arguments", json::object()}}));
    ASSERT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32602);
}

TEST_F(McpServerTest, BatchRequest_ReturnsBatchResponse)
{
    doInitialize();
    json batch = json::array({
        makeRequest("tools/list", json::object(), 1),
        makeRequest("tools/list", json::object(), 2)
    });
    auto resp = m_server->handleBatch(batch);
    ASSERT_TRUE(resp.is_array());
    EXPECT_EQ(resp.size(), 2u);
}

TEST_F(McpServerTest, BatchWithInitialize_Rejected)
{
    json batch = json::array({
        makeRequest("initialize", json::object(), 1),
        makeRequest("tools/list", json::object(), 2)
    });
    auto resp = m_server->handleBatch(batch);
    ASSERT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32600);
}

TEST_F(McpServerTest, ToolsCall_BeforeInitialize_ReturnsNotInitialized)
{
    auto resp = m_server->handleMessage(makeRequest("tools/call",
        {{"name", "echo_tool"}, {"arguments", {{"msg", "hi"}}}}));
    ASSERT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32002);
}

TEST_F(McpServerTest, ToolsList_BeforeInitialize_ReturnsNotInitialized)
{
    auto resp = m_server->handleMessage(makeRequest("tools/list"));
    ASSERT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32002);
}

TEST_F(McpServerTest, ToolsList_AfterInitialize_Succeeds)
{
    // Perform initialization handshake
    m_server->handleMessage(makeRequest("initialize"));
    json notif;
    notif["jsonrpc"] = "2.0";
    notif["method"] = "notifications/initialized";
    m_server->handleMessage(notif);

    auto resp = m_server->handleMessage(makeRequest("tools/list"));
    ASSERT_TRUE(resp.contains("result"));
    EXPECT_TRUE(resp["result"].contains("tools"));
}

TEST_F(McpServerTest, BatchWithNonObjectElement_ReturnsError)
{
    json batch = json::array({42, "bad"});
    auto resp = m_server->handleBatch(batch);
    ASSERT_TRUE(resp.is_array());
    EXPECT_EQ(resp.size(), 2u);
    EXPECT_EQ(resp[0]["error"]["code"], -32600);
    EXPECT_EQ(resp[1]["error"]["code"], -32600);
}

TEST_F(McpServerTest, BatchAllNotifications_ReturnsNull)
{
    json notif;
    notif["jsonrpc"] = "2.0";
    notif["method"] = "notifications/initialized";
    json batch = json::array({notif});
    auto resp = m_server->handleBatch(batch);
    EXPECT_TRUE(resp.is_null());
}
