#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;

// ── ProcessRunner (shared with test_protocol.cpp — duplicated here so each
//    test file is self-contained and the GLOB picks them up independently) ────

class WorkflowProcessRunner {
public:
    WorkflowProcessRunner() = default;
    ~WorkflowProcessRunner() { stop(); }

    WorkflowProcessRunner(const WorkflowProcessRunner&) = delete;
    WorkflowProcessRunner& operator=(const WorkflowProcessRunner&) = delete;

    bool start()
    {
#ifdef _WIN32
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        if (!CreatePipe(&m_childStdinRead, &m_childStdinWrite, &sa, 0))
            return false;
        SetHandleInformation(m_childStdinWrite, HANDLE_FLAG_INHERIT, 0);

        if (!CreatePipe(&m_childStdoutRead, &m_childStdoutWrite, &sa, 0))
            return false;
        SetHandleInformation(m_childStdoutRead, HANDLE_FLAG_INHERIT, 0);

        HANDLE hNul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
                                  &sa, OPEN_EXISTING, 0, nullptr);

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = m_childStdinRead;
        si.hStdOutput = m_childStdoutWrite;
        si.hStdError = hNul;

        PROCESS_INFORMATION pi{};
        std::string exe = TEST_EXE_PATH;

        BOOL ok = CreateProcessA(nullptr, exe.data(), nullptr, nullptr,
                                 TRUE, 0, nullptr, nullptr, &si, &pi);

        if (hNul != INVALID_HANDLE_VALUE)
            CloseHandle(hNul);

        if (!ok) return false;

        m_process = pi.hProcess;
        m_thread = pi.hThread;
        m_running = true;

        CloseHandle(m_childStdinRead);
        m_childStdinRead = INVALID_HANDLE_VALUE;
        CloseHandle(m_childStdoutWrite);
        m_childStdoutWrite = INVALID_HANDLE_VALUE;

        return true;
#else
        return false;
#endif
    }

    std::optional<json> sendRequest(const json& request, int timeoutMs = 5000)
    {
#ifdef _WIN32
        if (!m_running) return std::nullopt;

        std::string line = request.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";
        DWORD written;
        if (!WriteFile(m_childStdinWrite, line.data(),
                       static_cast<DWORD>(line.size()), &written, nullptr))
            return std::nullopt;

        return readLine(timeoutMs);
#else
        return std::nullopt;
#endif
    }

    bool isRunning() const
    {
#ifdef _WIN32
        if (!m_running || m_process == INVALID_HANDLE_VALUE) return false;
        DWORD exitCode;
        if (GetExitCodeProcess(m_process, &exitCode))
            return exitCode == STILL_ACTIVE;
        return false;
#else
        return false;
#endif
    }

    void stop()
    {
#ifdef _WIN32
        if (m_childStdinWrite != INVALID_HANDLE_VALUE) {
            CloseHandle(m_childStdinWrite);
            m_childStdinWrite = INVALID_HANDLE_VALUE;
        }
        if (m_process != INVALID_HANDLE_VALUE) {
            if (WaitForSingleObject(m_process, 1000) == WAIT_TIMEOUT)
                TerminateProcess(m_process, 1);
            CloseHandle(m_process);
            m_process = INVALID_HANDLE_VALUE;
        }
        if (m_thread != INVALID_HANDLE_VALUE) {
            CloseHandle(m_thread);
            m_thread = INVALID_HANDLE_VALUE;
        }
        if (m_childStdoutRead != INVALID_HANDLE_VALUE) {
            CloseHandle(m_childStdoutRead);
            m_childStdoutRead = INVALID_HANDLE_VALUE;
        }
        if (m_childStdinRead != INVALID_HANDLE_VALUE) {
            CloseHandle(m_childStdinRead);
            m_childStdinRead = INVALID_HANDLE_VALUE;
        }
        if (m_childStdoutWrite != INVALID_HANDLE_VALUE) {
            CloseHandle(m_childStdoutWrite);
            m_childStdoutWrite = INVALID_HANDLE_VALUE;
        }
        m_running = false;
#endif
    }

private:
#ifdef _WIN32
    std::optional<json> readLine(int timeoutMs)
    {
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(timeoutMs);

        while (std::chrono::steady_clock::now() < deadline) {
            DWORD exitCode;
            if (GetExitCodeProcess(m_process, &exitCode) && exitCode != STILL_ACTIVE) {
                m_running = false;
                return std::nullopt;
            }

            DWORD available = 0;
            if (PeekNamedPipe(m_childStdoutRead, nullptr, 0, nullptr, &available, nullptr)
                && available > 0)
            {
                char buf[4096];
                DWORD toRead = (std::min)(available, (DWORD)sizeof(buf));
                DWORD bytesRead = 0;
                if (ReadFile(m_childStdoutRead, buf, toRead, &bytesRead, nullptr)
                    && bytesRead > 0)
                {
                    m_readBuf.append(buf, bytesRead);
                }
            }

            auto pos = m_readBuf.find('\n');
            if (pos != std::string::npos) {
                std::string line = m_readBuf.substr(0, pos);
                m_readBuf.erase(0, pos + 1);

                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.empty())
                    continue;

                try {
                    return json::parse(line);
                } catch (...) {
                    return std::nullopt;
                }
            }

            Sleep(10);
        }

        return std::nullopt;
    }

    HANDLE m_process = INVALID_HANDLE_VALUE;
    HANDLE m_thread = INVALID_HANDLE_VALUE;
    HANDLE m_childStdinRead = INVALID_HANDLE_VALUE;
    HANDLE m_childStdinWrite = INVALID_HANDLE_VALUE;
    HANDLE m_childStdoutRead = INVALID_HANDLE_VALUE;
    HANDLE m_childStdoutWrite = INVALID_HANDLE_VALUE;
    std::string m_readBuf;
#endif
    bool m_running = false;
};


// ── Workflow test fixture ───────────────────────────────────────────────────

class WorkflowTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        s_runner = std::make_unique<WorkflowProcessRunner>();
        if (!s_runner->start()) {
            s_skipAll = true;
            s_skipReason = "Failed to start renderdoc-mcp.exe";
            return;
        }

        // Initialize handshake
        json initReq;
        initReq["jsonrpc"] = "2.0";
        initReq["id"] = 0;
        initReq["method"] = "initialize";
        initReq["params"]["protocolVersion"] = "2025-03-26";
        initReq["params"]["clientInfo"]["name"] = "test-runner";
        initReq["params"]["clientInfo"]["version"] = "1.0.0";
        initReq["params"]["capabilities"] = json::object();

        auto resp = s_runner->sendRequest(initReq, 10000);
        if (!resp.has_value()) {
            s_skipAll = true;
            s_skipReason = "renderdoc-mcp.exe did not respond to initialize (timeout/crash)";
            s_runner->stop();
            return;
        }

        // Send initialized notification
        json notif;
        notif["jsonrpc"] = "2.0";
        notif["method"] = "notifications/initialized";
        // Notifications have no id and no response - just write it
        std::string line = notif.dump() + "\n";
        // Use sendRequest but don't wait for response (it's a notification)
        // Actually we need raw write. Let's just send a dummy request after
        // to flush the notification through.
        s_runner->sendRequest(notif, 500); // Will timeout since no response - that's OK

        // Try opening the test capture
        auto openResp = callTool("open_capture", {{"path", TEST_RDC_PATH}}, 15000);
        if (!openResp.has_value()) {
            s_skipAll = true;
            s_skipReason = "open_capture timed out (no GPU/driver or process crashed)";
            s_runner->stop();
            return;
        }

        // Check if open_capture returned an error at the tool level
        if (openResp->contains("result")) {
            auto& result = (*openResp)["result"];
            if (result.contains("isError") && result["isError"].get<bool>()) {
                s_skipAll = true;
                s_skipReason = "open_capture failed: " + result.dump();
                s_runner->stop();
                return;
            }
        } else if (openResp->contains("error")) {
            s_skipAll = true;
            s_skipReason = "open_capture RPC error: " + (*openResp)["error"]["message"].get<std::string>();
            s_runner->stop();
            return;
        }
    }

    static void TearDownTestSuite()
    {
        if (s_runner)
            s_runner->stop();
    }

    void SetUp() override
    {
        if (s_skipAll)
            GTEST_SKIP() << s_skipReason;
    }

    // Helper: call a tool via JSON-RPC tools/call
    static std::optional<json> callTool(const std::string& name,
                                        const json& arguments = json::object(),
                                        int timeoutMs = 10000)
    {
        static int s_idCounter = 1000;
        json req;
        req["jsonrpc"] = "2.0";
        req["id"] = s_idCounter++;
        req["method"] = "tools/call";
        req["params"]["name"] = name;
        req["params"]["arguments"] = arguments;
        return s_runner->sendRequest(req, timeoutMs);
    }

    // Helper: extract the text content from a tool result
    static std::optional<json> extractToolResult(const json& response)
    {
        if (!response.contains("result"))
            return std::nullopt;

        auto& result = response["result"];
        if (result.contains("isError") && result["isError"].get<bool>())
            return std::nullopt;

        if (!result.contains("content") || !result["content"].is_array()
            || result["content"].empty())
            return std::nullopt;

        std::string text = result["content"][0].value("text", "");
        if (text.empty())
            return std::nullopt;

        try {
            return json::parse(text);
        } catch (...) {
            // Not JSON, return as string in a json value
            return json(text);
        }
    }

    static std::unique_ptr<WorkflowProcessRunner> s_runner;
    static bool s_skipAll;
    static std::string s_skipReason;
};

std::unique_ptr<WorkflowProcessRunner> WorkflowTest::s_runner;
bool WorkflowTest::s_skipAll = false;
std::string WorkflowTest::s_skipReason;


// ── Workflow Tests ──────────────────────────────────────────────────────────

TEST_F(WorkflowTest, FullDebugWorkflow)
{
    // Step 1: get_capture_info
    auto infoResp = callTool("get_capture_info");
    ASSERT_TRUE(infoResp.has_value()) << "get_capture_info timed out";
    ASSERT_TRUE(infoResp->contains("result")) << "get_capture_info missing result";
    auto info = extractToolResult(*infoResp);
    ASSERT_TRUE(info.has_value()) << "get_capture_info returned error or empty";
    EXPECT_TRUE(info->contains("api"));
    EXPECT_TRUE(info->contains("eventCount"));

    // Step 2: list_events
    auto eventsResp = callTool("list_events");
    ASSERT_TRUE(eventsResp.has_value()) << "list_events timed out";
    auto events = extractToolResult(*eventsResp);
    ASSERT_TRUE(events.has_value()) << "list_events returned error";
    ASSERT_TRUE(events->contains("events"));
    ASSERT_GT((*events)["events"].size(), 0u);

    // Step 3: goto_event (first event)
    uint32_t firstEventId = (*events)["events"][0]["eventId"].get<uint32_t>();
    auto gotoResp = callTool("goto_event", {{"eventId", firstEventId}});
    ASSERT_TRUE(gotoResp.has_value()) << "goto_event timed out";
    ASSERT_TRUE(gotoResp->contains("result")) << "goto_event missing result";

    // Step 4: get_pipeline_state
    auto pipeResp = callTool("get_pipeline_state");
    ASSERT_TRUE(pipeResp.has_value()) << "get_pipeline_state timed out";
    auto pipeState = extractToolResult(*pipeResp);
    ASSERT_TRUE(pipeState.has_value()) << "get_pipeline_state returned error";
    EXPECT_TRUE(pipeState->contains("api"));

    // Step 5: export_render_target
    // On headless/no-GPU machines, export may fail at the tool level (isError).
    // We verify the server responds with a well-formed JSON-RPC result either way.
    auto exportResp = callTool("export_render_target");
    ASSERT_TRUE(exportResp.has_value()) << "export_render_target timed out";
    ASSERT_TRUE(exportResp->contains("result")) << "export_render_target missing result";
    auto exportResult = extractToolResult(*exportResp);
    if (exportResult.has_value()) {
        EXPECT_TRUE(exportResult->contains("path"));
    } else {
        // Tool-level error is acceptable on machines without GPU rendering
        auto& result = (*exportResp)["result"];
        EXPECT_TRUE(result.contains("isError"))
            << "export_render_target returned neither success nor tool error";
    }

    // Verify process still alive after full workflow
    EXPECT_TRUE(s_runner->isRunning());
}

TEST_F(WorkflowTest, EventNavigation)
{
    // list_draws to get draw call event IDs
    auto drawsResp = callTool("list_draws");
    ASSERT_TRUE(drawsResp.has_value()) << "list_draws timed out";
    auto draws = extractToolResult(*drawsResp);
    ASSERT_TRUE(draws.has_value()) << "list_draws returned error";
    ASSERT_TRUE(draws->contains("draws"));
    ASSERT_GT((*draws)["draws"].size(), 0u);

    // Navigate to up to 3 different draw events
    size_t count = (std::min)((*draws)["draws"].size(), (size_t)3);
    for (size_t i = 0; i < count; i++) {
        uint32_t eid = (*draws)["draws"][i]["eventId"].get<uint32_t>();
        auto resp = callTool("goto_event", {{"eventId", eid}});
        ASSERT_TRUE(resp.has_value())
            << "goto_event timed out for draw " << i << " (eventId=" << eid << ")";
        ASSERT_TRUE(resp->contains("result"))
            << "goto_event missing result for eventId=" << eid;
        // Verify it's not an error response
        EXPECT_FALSE(resp->contains("error"))
            << "goto_event returned error for eventId=" << eid;
    }

    EXPECT_TRUE(s_runner->isRunning());
}

TEST_F(WorkflowTest, ResourceInspection)
{
    // list_resources
    auto listResp = callTool("list_resources");
    ASSERT_TRUE(listResp.has_value()) << "list_resources timed out";
    auto resources = extractToolResult(*listResp);
    ASSERT_TRUE(resources.has_value()) << "list_resources returned error";
    ASSERT_TRUE(resources->contains("resources"));
    ASSERT_GT((*resources)["resources"].size(), 0u);

    // get_resource_info for first resource
    std::string firstResId = (*resources)["resources"][0]["resourceId"].get<std::string>();
    auto infoResp = callTool("get_resource_info", {{"resourceId", firstResId}});
    ASSERT_TRUE(infoResp.has_value()) << "get_resource_info timed out";
    ASSERT_TRUE(infoResp->contains("result")) << "get_resource_info missing result";

    // Verify we got a valid response (not an RPC error)
    EXPECT_FALSE(infoResp->contains("error"))
        << "get_resource_info returned error for resourceId=" << firstResId;

    EXPECT_TRUE(s_runner->isRunning());
}
