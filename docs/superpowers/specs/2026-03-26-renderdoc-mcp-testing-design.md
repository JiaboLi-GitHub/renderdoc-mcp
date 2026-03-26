# renderdoc-mcp 自动化测试设计

## 概述

为 renderdoc-mcp 项目（约 3000 行 C++，20 个 MCP 工具）建立全面的自动化测试体系，采用混合分层架构：进程内单元测试 + 进程外集成测试，使用 GoogleTest 框架，包含 GitHub Actions CI。

## 架构

```
测试层次
├── Layer 1: 进程内单元测试 (链接 renderdoc-mcp-lib)
│   ├── 协议层测试 (test_mcp_server.cpp) — 不需要 renderdoc
│   ├── 参数验证测试 (test_tool_registry.cpp) — 不需要 renderdoc
│   └── 工具逻辑测试 (test_tools.cpp) — 需要 renderdoc.dll + .rdc
└── Layer 2: 进程外集成测试 (启动 renderdoc-mcp.exe 子进程)
    ├── 协议端到端测试 (test_protocol.cpp) — 需要 exe
    └── 工作流测试 (test_workflow.cpp) — 需要 exe + renderdoc.dll + .rdc
```

## 项目结构变更

```
renderdoc-mcp/
├── CMakeLists.txt              # 修改：添加 renderdoc-mcp-lib target + tests subdirectory
├── src/
│   ├── main.cpp                # 仅用于 exe target
│   └── ...                     # 其他源文件编入 renderdoc-mcp-lib 静态库
├── tests/
│   ├── CMakeLists.txt          # 测试构建配置
│   ├── unit/
│   │   ├── test_mcp_server.cpp
│   │   ├── test_tool_registry.cpp
│   │   └── test_tools.cpp
│   ├── integration/
│   │   ├── test_protocol.cpp
│   │   └── test_workflow.cpp
│   └── fixtures/
│       └── simple_triangle.rdc # 测试用 .rdc 文件 (< 500KB)
├── .github/
│   └── workflows/
│       └── ci.yml
```

## 构建系统变更

### CMakeLists.txt 主要变更

1. **静态库 target**：将 `src/` 中除 `main.cpp` 以外的所有文件编译为 `renderdoc-mcp-lib`
2. **exe target**：链接 `renderdoc-mcp-lib` + `main.cpp`
3. **测试 subdirectory**：通过 `add_subdirectory(tests)` 引入
4. **CMake option**：`BUILD_TESTING` (ON/OFF) 控制是否构建测试

### tests/CMakeLists.txt

- FetchContent 引入 GoogleTest (v1.14.0)
- 测试 target 链接 `renderdoc-mcp-lib` + `GTest::gtest_main`
- CMake option `RENDERDOC_TEST_CAPTURE` 指定测试 .rdc 文件路径
- 用 `configure_file` 或 `target_compile_definitions` 传入 exe 路径和 .rdc 路径
- CTest label 区分：
  - `unit` — 不需要 renderdoc 环境
  - `integration` — 需要 renderdoc 环境

## Layer 1：进程内单元测试

### 1.1 协议层测试 (test_mcp_server.cpp)

不需要 renderdoc.dll，可在任何环境运行。

**测试用例：**

| 测试 | 描述 |
|------|------|
| Initialize_ReturnsServerInfo | 验证 initialize 返回 serverInfo、capabilities、protocolVersion |
| Initialize_HasToolsCapability | 验证 capabilities 包含 tools |
| ToolsList_Returns20Tools | 验证 tools/list 返回 20 个工具定义 |
| ToolsList_EachHasRequiredFields | 每个工具有 name、description、inputSchema |
| ToolsCall_UnknownTool_ReturnsError | 调用不存在的工具返回错误 |
| ToolsCall_ValidTool_DispatchesCorrectly | 验证路由到正确的工具处理函数 |
| UnknownMethod_ReturnsMethodNotFound | 未知 method 返回 -32601 |
| InvalidParams_Returns32602 | 参数验证失败返回 -32602 |
| ToolException_ReturnsIsError | 工具内部异常返回 isError: true（非协议错误） |
| BatchRequest_ReturnsBatchResponse | JSON 数组请求返回数组响应 |
| BatchWithInitialize_Rejected | 批处理中包含 initialize 被拒绝 |

**实现方式：**
- 直接实例化 `McpServer`，调用其 `handleRequest(json)` 方法
- 不启动消息循环，不涉及 stdio

### 1.2 参数验证测试 (test_tool_registry.cpp)

不需要 renderdoc.dll，可在任何环境运行。

**测试用例：**

| 测试 | 描述 |
|------|------|
| RequiredFieldMissing_ThrowsInvalidParams | 每个工具的 required 参数缺失时抛错 |
| WrongType_String_ThrowsInvalidParams | string 字段传 int 时抛错 |
| WrongType_Integer_ThrowsInvalidParams | integer 字段传 string 时抛错 |
| EnumValidation_InvalidValue_Throws | 枚举参数传无效值时抛错 |
| EnumValidation_ValidValue_Passes | 枚举参数传有效值时通过 |
| OptionalField_Absent_NoError | 可选参数缺省时不报错 |
| AllTools_HaveValidSchema | 所有 20 个工具的 inputSchema 格式正确 |
| CallTool_UnknownName_Throws | 调用不存在的工具名抛错 |

**参数化测试：**
- 用 `INSTANTIATE_TEST_SUITE_P` 对所有 20 个工具批量验证 required 字段
- 每个工具的 inputSchema 自动提取 required 字段列表

### 1.3 工具逻辑测试 (test_tools.cpp)

需要 renderdoc.dll + 测试 .rdc 文件。CTest label: `integration`。

**Fixture：**
```cpp
class RenderdocToolTest : public ::testing::Test {
protected:
    static void SetUpTestSuite();   // 打开 .rdc 文件，初始化 RenderdocWrapper
    static void TearDownTestSuite(); // 关闭 capture
    static RenderdocWrapper wrapper;
    static ToolRegistry registry;
};
```

**测试用例（每个工具至少 1 个正向 + 1 个异常）：**

| 工具 | 正向测试 | 异常测试 |
|------|---------|---------|
| open_capture | 打开文件返回 API 类型和事件数 | 打开不存在的文件返回错误 |
| list_events | 返回非空事件列表 | — |
| goto_event | 导航到有效 eventId | 无效 eventId 返回错误 |
| list_draws | 返回 draw calls，有正确字段 | — |
| get_draw_info | 返回 draw 详情 | 无效 eventId 返回错误 |
| get_pipeline_state | 返回 shader stages | 未 goto_event 时行为验证 |
| get_bindings | 返回 CBV/SRV/UAV 绑定 | 无效 stage 返回错误 |
| get_shader | 返回 disassembly 或 reflection | 无绑定 shader 的 stage |
| list_shaders | 列出所有 unique shaders | — |
| search_shaders | 搜索 shader 文本 | 无匹配时返回空列表 |
| list_resources | 返回资源列表 | — |
| get_resource_info | 返回资源元数据 | 无效 ResourceId |
| list_passes | 返回 render pass 列表 | — |
| get_pass_info | 返回 pass 详情 | 无效 passIndex |
| export_render_target | 导出 PNG 文件存在且非空 | — |
| export_texture | 导出纹理 PNG | 无效 ResourceId |
| export_buffer | 导出 buffer 二进制文件 | 无效 ResourceId |
| get_capture_info | 返回 API、GPU、驱动信息 | — |
| get_stats | 返回性能统计 | — |
| get_log | 返回日志消息 | — |

## Layer 2：进程外集成测试

### 2.1 进程管理辅助类

```cpp
class ProcessRunner {
public:
    ProcessRunner(const std::string& exePath);
    ~ProcessRunner();  // 终止进程

    void start();
    void stop();
    json sendRequest(const json& request, int timeoutMs = 5000);
    json sendBatch(const std::vector<json>& requests, int timeoutMs = 5000);

private:
    // Win32 CreateProcess + 匿名管道
    HANDLE m_process;
    HANDLE m_stdinWrite;
    HANDLE m_stdoutRead;
};
```

- 用 Win32 `CreateProcess` + 匿名管道实现 stdin/stdout 重定向
- 读取超时 5 秒防止死锁
- 每个 JSON-RPC 消息以 `\n` 分隔

### 2.2 协议端到端测试 (test_protocol.cpp)

CTest label: `integration`。

| 测试 | 描述 |
|------|------|
| InitializeHandshake | 发送 initialize，验证完整 JSON-RPC 响应 |
| ToolsListComplete | tools/list 返回 20 个工具 |
| ParseError_MalformedJson | 发送畸形 JSON → -32700 |
| MethodNotFound_UnknownMethod | 未知 method → -32601 |
| BatchRequest_ArrayResponse | 批处理请求返回对应数组 |
| ProcessStable_MultipleRequests | 连续发送多个请求，进程稳定 |

### 2.3 工作流测试 (test_workflow.cpp)

模拟真实 AI 客户端的完整调试流程。CTest label: `integration`。

| 测试 | 描述 |
|------|------|
| FullDebugWorkflow | initialize → open_capture → get_capture_info → list_events → goto_event → get_pipeline_state → export_render_target → 验证 PNG 存在 |
| EventNavigation | 遍历多个事件，验证 goto_event 在不同 eventId 下行为正确 |
| ResourceInspection | list_resources → get_resource_info → export_texture 流程 |

## CI/CD

### GitHub Actions (.github/workflows/ci.yml)

```yaml
触发: push (main) + pull_request
环境: windows-latest
步骤:
  1. Checkout
  2. 下载 renderdoc Release (带 cache)
  3. CMake configure (-DBUILD_TESTING=ON -DRENDERDOC_DIR=...)
  4. CMake build (Release)
  5. CTest -L unit (必跑，不需要 renderdoc 环境)
  6. CTest -L integration (条件执行，需 renderdoc.dll + .rdc)
```

### renderdoc 依赖处理

- **renderdoc.dll**：从 renderdoc 官方 GitHub Release 下载预编译版本，用 Actions cache 加速
- **.rdc 文件**：提交到 `tests/fixtures/`（< 500KB）
- **条件执行**：CTest label 区分 `unit` 和 `integration`，CI 中 unit 必跑，integration 视 DLL 是否就绪

### 本地测试命令

```bash
# 仅不需要 renderdoc 的单元测试
ctest --test-dir build -L unit

# 所有测试
ctest --test-dir build

# 仅集成测试
ctest --test-dir build -L integration
```

## 测试 .rdc 文件

- 用 RenderDoc 抓取最简单的 D3D11 三角形渲染帧
- 目标大小 < 500KB
- 存放路径 `tests/fixtures/simple_triangle.rdc`
- 需包含至少：1 个 draw call、1 个 VS + PS shader、1 个 render target

## 不在本设计范围内

- Mock renderdoc API（使用真实文件）
- 代码覆盖率工具（后续增量添加）
- 性能/压力测试
- 跨平台支持（当前仅 Windows/MSVC）
