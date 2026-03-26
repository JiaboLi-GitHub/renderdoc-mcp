# renderdoc-mcp 自动化测试设计

## 概述

为 renderdoc-mcp 项目（约 3000 行 C++，20 个 MCP 工具）建立全面的自动化测试体系，采用混合分层架构：进程内单元测试 + 进程外集成测试，使用 GoogleTest 框架，包含 GitHub Actions CI。

## 架构

### 运行时依赖分析

`renderdoc_wrapper.cpp` 隐式链接 `renderdoc.lib`（import library）。在 Windows 上，隐式链接导致 DLL 在进程启动时加载 — 即使测试代码不调用 renderdoc API，只要可执行文件链接了 `renderdoc.lib`，进程启动就需要 `renderdoc.dll`。

因此，将源码拆分为两个库 target：

- **`renderdoc-mcp-core`**（静态库）：`mcp_server.cpp` + `tool_registry.cpp`。仅依赖 nlohmann/json，不链接 renderdoc。`ToolRegistry` 本身零 renderdoc 依赖（仅前向声明 `RenderdocWrapper`），`McpServer` 通过前向声明使用 `RenderdocWrapper` 指针，编译不需要 renderdoc 头文件。
- **`renderdoc-mcp-lib`**（静态库）：`renderdoc_wrapper.cpp` + `tools/*.cpp`。链接 `renderdoc.lib`，依赖 renderdoc 头文件。

注意：`McpServer` 当前持有 `RenderdocWrapper m_wrapper` 值成员（非指针），且构造函数调用 `registerXxxTools(m_registry)`，这两处依赖导致 `mcp_server.cpp` 的编译需要 `RenderdocWrapper` 完整定义和 tools 注册函数。**需要生产代码小幅重构**（见下文"生产代码变更"节）使 `mcp_server.cpp` 可脱离 renderdoc 编译。

### 生产代码变更（为测试分层所需）

1. **`McpServer` 构造函数注入**：新增构造函数 `McpServer(ToolRegistry& registry, RenderdocWrapper& wrapper)`，允许外部注入 registry 和 wrapper 引用。原无参构造函数保留，内部创建自有 registry/wrapper 并调用 `registerXxxTools`。
2. **`mcp_server.h` 解耦**：将 `m_wrapper` 和 `m_registry` 改为引用或指针，`RenderdocWrapper` 改回前向声明（不 include `renderdoc_wrapper.h`）。
3. 这样 `mcp_server.cpp` 编译不再需要 renderdoc 头文件，可归入 `renderdoc-mcp-core`。

### 测试层次

```
测试层次
├── Layer 1a: 纯单元测试 (链接 renderdoc-mcp-core，不链接 renderdoc)
│   ├── test_tool_registry.cpp — 用手动注册的 dummy 工具测试 JSON Schema 校验
│   └── test_mcp_server.cpp — 注入 dummy registry 测试协议层
│   → CTest label: unit，运行时零 renderdoc 依赖
│
├── Layer 1b: 工具逻辑测试 (链接 renderdoc-mcp-core + renderdoc-mcp-lib)
│   └── test_tools.cpp — 需要 renderdoc.dll + .rdc
│   → CTest label: integration
│
└── Layer 2: 进程外集成测试 (启动 renderdoc-mcp.exe 子进程)
    ├── test_protocol.cpp — 需要 exe + renderdoc.dll
    └── test_workflow.cpp — 需要 exe + renderdoc.dll + .rdc
    → CTest label: integration
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
│   │   └── renderdoc_wrapper_stub.cpp  # RenderdocWrapper 空壳实现，仅用于 test-unit 链接
│   ├── integration/
│   │   ├── test_protocol.cpp
│   │   └── test_workflow.cpp
│   └── fixtures/
│       └── vkcube.rdc          # Vulkan vkcube 抓帧，测试用 .rdc 文件 (134KB)
├── .github/
│   └── workflows/
│       └── ci.yml
```

## 构建系统变更

### CMakeLists.txt 主要变更

1. **`renderdoc-mcp-core`**（静态库）：`mcp_server.cpp` + `tool_registry.cpp`，仅链接 `nlohmann_json`，不链接 renderdoc
2. **`renderdoc-mcp-lib`**（静态库）：`renderdoc_wrapper.cpp` + `tools/*.cpp`，链接 `renderdoc.lib` + `renderdoc-mcp-core`
3. **exe target**：链接 `renderdoc-mcp-lib` + `main.cpp`（`renderdoc-mcp-core` 通过传递依赖自动链入）
4. **测试 subdirectory**：通过 `add_subdirectory(tests)` 引入
5. **CMake option**：`BUILD_TESTING` (ON/OFF) 控制是否构建测试

### tests/CMakeLists.txt

- FetchContent 引入 GoogleTest (v1.14.0)
- **三个测试可执行文件，按依赖分离：**
  - `test-unit`：链接 `renderdoc-mcp-core` + `renderdoc_wrapper_stub.cpp` + GTest。包含 `test_mcp_server.cpp` 和 `test_tool_registry.cpp`。**不链接 renderdoc.lib，运行时零 renderdoc 依赖**
  - `test-tools`：链接 `renderdoc-mcp-lib` + GTest。包含 `test_tools.cpp`。运行时需 renderdoc.dll + .rdc 文件
  - `test-integration`：链接 GTest（不链接任何项目库）。包含 `test_protocol.cpp`、`test_workflow.cpp`、`ProcessRunner` 辅助类。通过子进程方式调用 exe，运行时需 renderdoc.dll + .rdc 文件
- CMake option `RENDERDOC_TEST_CAPTURE` 指定测试 .rdc 文件路径，默认为 `${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vkcube.rdc`
- 用 `target_compile_definitions` 传入路径：
  - `-DTEST_RDC_PATH="${RENDERDOC_TEST_CAPTURE}"` — .rdc 文件路径
  - `-DTEST_EXE_PATH="$<TARGET_FILE:renderdoc-mcp>"` — exe 路径（用 generator expression）
- 用 `gtest_discover_tests()` 自动发现测试用例，再通过 `set_tests_properties` 按 test executable 分配 CTest label
- CTest label 区分：
  - `unit` — `test-unit` 的所有用例，运行时零 renderdoc 依赖
  - `integration` — `test-tools` 和 `test-integration` 的所有用例，需 renderdoc.dll

## Layer 1a：纯单元测试（零 renderdoc 依赖）

可执行文件 `test-unit`，仅链接 `renderdoc-mcp-core` + GTest。编译和运行均不需要 renderdoc 头文件、renderdoc.lib 或 renderdoc.dll。

### RenderdocWrapper 链接桩

`ToolRegistry::callTool` 和 `McpServer` 的注入构造函数都接受 `RenderdocWrapper&` 参数。虽然 dummy handler 不使用 wrapper，但 `test-unit` 需要能构造/析构 `RenderdocWrapper` 对象以满足链接器。

`RenderdocWrapper` 的析构函数（及 `shutdown()`、`closeCurrent()` 等）定义在 `renderdoc_wrapper.cpp` 中，会调用 renderdoc API，导致链接时拉入 `renderdoc.lib`。

**解决方案：** 在 `tests/unit/` 中提供 `renderdoc_wrapper_stub.cpp`，仅包含 `RenderdocWrapper` 的空壳实现：

```cpp
// tests/unit/renderdoc_wrapper_stub.cpp
// 为 test-unit 提供 RenderdocWrapper 的链接桩，使其不依赖 renderdoc.lib
#include "renderdoc_wrapper.h"

RenderdocWrapper::~RenderdocWrapper() {}
void RenderdocWrapper::shutdown() {}
// 其他被引用的成员函数也提供空实现（如 closeCurrent 等）
// 这些实现永远不会被真正调用，仅用于满足链接器
```

`test-unit` 链接 `renderdoc-mcp-core` + `renderdoc_wrapper_stub.o` + GTest，不链接 `renderdoc_wrapper.o`，从而完全切断 renderdoc 依赖链。

### 1.1 协议层测试 (test_mcp_server.cpp)

**实现方式：**
- 利用注入构造函数 `McpServer(ToolRegistry& registry, RenderdocWrapper& wrapper)` 传入测试控制的 registry
- 在 registry 中注册 dummy 工具（handler 返回固定 JSON 或抛出预设异常），不涉及真实 renderdoc 调用
- `RenderdocWrapper` 参数传入一个默认构造的实例（由 stub 提供空壳实现，dummy handler 不使用它）

**测试用例：**

| 测试 | 描述 |
|------|------|
| Initialize_ReturnsServerInfo | 验证 initialize 返回 serverInfo、capabilities、protocolVersion |
| Initialize_HasToolsCapability | 验证 capabilities 包含 tools |
| ToolsList_ReturnsRegisteredTools | 验证 tools/list 返回注入的 dummy 工具定义 |
| ToolsList_EachHasRequiredFields | 每个工具有 name、description、inputSchema |
| ToolsCall_UnknownTool_ReturnsError | 调用不存在的工具返回 -32602 |
| ToolsCall_ValidTool_ReturnsHandlerResult | 调用 dummy 工具，验证 handler 返回值被正确包装为 MCP tool result |
| ToolsCall_HandlerThrowsRuntime_ReturnsIsError | dummy handler 抛 runtime_error → 响应包含 isError: true |
| ToolsCall_HandlerThrowsInvalidParams_Returns32602 | dummy handler 抛 InvalidParamsError → 协议级 -32602 |
| UnknownMethod_ReturnsMethodNotFound | 未知 method 返回 -32601 |
| InvalidParams_MissingToolName_Returns32602 | tools/call 缺 name 参数返回 -32602 |
| BatchRequest_ReturnsBatchResponse | JSON 数组请求返回数组响应 |
| BatchWithInitialize_Rejected | 批处理中包含 initialize 被拒绝 |

注：原 `ToolsCall_ValidTool_DispatchesCorrectly` 已拆分为 `ToolsCall_ValidTool_ReturnsHandlerResult`（验证正向 dispatch 路径）和两个异常路径用例。通过注入 dummy handler，可完全验证 McpServer 的 dispatch + 错误映射逻辑，无需真实工具。

### 1.2 参数验证测试 (test_tool_registry.cpp)

**实现方式：**
- 直接实例化 `ToolRegistry`，手动注册带有各种 inputSchema 的 dummy 工具
- 测试 `validateArgs` 的每个分支（required、type、enum）
- 不需要 McpServer 或 RenderdocWrapper

**测试用例：**

| 测试 | 描述 |
|------|------|
| RequiredFieldMissing_ThrowsInvalidParams | required 参数缺失时抛错 |
| WrongType_String_ThrowsInvalidParams | string 字段传 int 时抛错 |
| WrongType_Integer_ThrowsInvalidParams | integer 字段传 string 时抛错 |
| WrongType_Boolean_ThrowsInvalidParams | boolean 字段传 string 时抛错 |
| EnumValidation_InvalidValue_Throws | 枚举参数传无效值时抛错 |
| EnumValidation_ValidValue_Passes | 枚举参数传有效值时通过 |
| OptionalField_Absent_NoError | 可选参数缺省时不报错 |
| UnknownField_Ignored | 传入 schema 未定义的字段不报错 |
| CallTool_UnknownName_Throws | 调用不存在的工具名抛 InvalidParamsError |
| HasTool_RegisteredTool_ReturnsTrue | 已注册工具返回 true |
| HasTool_UnknownTool_ReturnsFalse | 未注册工具返回 false |

## Layer 1b：工具逻辑测试（需 renderdoc）

可执行文件 `test-tools`，链接 `renderdoc-mcp-core` + `renderdoc-mcp-lib` + GTest。运行时需要 renderdoc.dll + .rdc 文件。CTest label: `integration`。

### 1.3 工具逻辑测试 (test_tools.cpp)

**Fixture：**
```cpp
class RenderdocToolTest : public ::testing::Test {
protected:
    static void SetUpTestSuite();   // 打开 .rdc 文件，初始化 RenderdocWrapper，
                                    // 并 gotoEvent 到第一个 draw call
    static void TearDownTestSuite(); // 关闭 capture
    static RenderdocWrapper wrapper;
    static ToolRegistry registry;
};
```

**注意：** 由于 fixture 使用 static 共享状态，某些测试（如 goto_event）会修改 wrapper 的当前事件。测试间存在隐式顺序依赖，不可使用 `--gtest_shuffle`。SetUpTestSuite 会将状态初始化到已知的 draw call 位置，单个测试修改状态后应在各自的 TearDown 或测试末尾恢复。

**测试用例（每个工具至少 1 个正向 + 1 个异常）：**

| 工具 | 正向测试 | 异常测试 |
|------|---------|---------|
| open_capture | 打开文件返回 API 类型和事件数 | 打开不存在的文件返回错误 |
| list_events | 返回非空事件列表 | 无效 filter 时返回空列表 |
| goto_event | 导航到有效 eventId | 无效 eventId 返回错误 |
| list_draws | 返回 draw calls，有 eventId/name/flags/numIndices/numInstances 字段 | filter 传不匹配的关键词返回空列表 |
| get_draw_info | 传有效 eventId 返回 draw 详情 | 无效 eventId 返回错误 |
| get_pipeline_state | 无参调用返回当前事件的 shader stages；传 eventId 参数返回指定事件状态 | 未打开 capture 时抛错 |
| get_bindings | 无参调用返回当前事件各 stage 的绑定信息（constantBuffers/readOnlyResources/readWriteResources/samplers）；传 eventId 参数返回指定事件 | 未打开 capture 时抛错 |
| get_shader | 返回 disassembly 或 reflection | 无绑定 shader 的 stage 返回空 |
| list_shaders | 列出所有 unique shaders | 验证返回结果有 stage 和 usageCount 字段 |
| search_shaders | 搜索 shader 文本 | 无匹配时返回空列表 |
| list_resources | 返回资源列表 | — |
| get_resource_info | 返回资源元数据 | 无效 ResourceId |
| list_passes | 返回 render pass 列表 | — |
| get_pass_info | 传有效 eventId 返回 pass 详情（draws 列表、drawCount、dispatchCount） | 无效 eventId 返回错误 |
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
- 每个 JSON-RPC 消息以 `\n` 分隔（与 main.cpp 一致）
- 读取策略：按字节累积直到遇到 `\n` 分隔符，处理部分读取的情况
- 子进程崩溃检测：读取失败时检查进程状态，报告退出码

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

CI 分两个 job，依赖关系不同：

```yaml
触发: push (main) + pull_request
环境: windows-latest

Job 1: unit-tests（快速，无 renderdoc 依赖）
  步骤:
    1. Checkout
    2. CMake configure (-DBUILD_TESTING=ON，不设 RENDERDOC_DIR)
       → 仅构建 renderdoc-mcp-core + test-unit，跳过需要 renderdoc 的 target
    3. CMake build (Release，target: test-unit)
    4. CTest -L unit

Job 2: integration-tests（需 renderdoc 全套）
  步骤:
    1. Checkout
    2. 获取 renderdoc（shallow clone + 构建，或从 cache 恢复）
    3. CMake configure (-DBUILD_TESTING=ON -DRENDERDOC_DIR=... -DRENDERDOC_BUILD_DIR=...)
       → 构建所有 target
    4. CMake build (Release)
    5. CTest -L integration
```

### renderdoc 依赖处理

`renderdoc-mcp-core`（及 `test-unit`）**完全不需要 renderdoc**，编译和运行均独立。

`renderdoc-mcp-lib`、`test-tools`、`test-integration` 和 `renderdoc-mcp.exe` 需要 renderdoc 的三样东西：头文件（`renderdoc/api/replay/` 下）、`renderdoc.lib`、`renderdoc.dll`。renderdoc 官方 GitHub Release 仅包含应用程序二进制文件，不含 replay API 开发头文件和 .lib。

**方案：CI 中 shallow clone renderdoc 源码 + 构建 renderdoc.lib：**
1. `git clone --depth 1` renderdoc 仓库（约 50MB shallow clone）
2. CMake configure + build renderdoc 的 renderdoc 目标（仅 lib + dll）
3. 用 GitHub Actions cache 缓存构建结果，按 renderdoc commit hash 做 key
4. `RENDERDOC_DIR` 指向 clone 的源码根目录，`RENDERDOC_BUILD_DIR` 指向构建输出

**备选方案：如果 renderdoc 构建耗时过长，可考虑将预编译的 renderdoc.lib + renderdoc.dll + 必要头文件打包为 GitHub Release asset 存放在 renderdoc-mcp 仓库中。**

- **.rdc 文件**：提交到 `tests/fixtures/`（< 500KB）

### CMakeLists.txt 对无 RENDERDOC_DIR 的处理

当前 `CMakeLists.txt` 在 `RENDERDOC_DIR` 未设置时 `FATAL_ERROR`。需要修改为：
- `RENDERDOC_DIR` 未设置时，仅构建 `renderdoc-mcp-core`（和 `test-unit`），跳过依赖 renderdoc 的 target
- 这使得 CI Job 1 可以在无 renderdoc 环境下快速构建和运行纯单元测试

### 本地测试命令

```bash
# 纯单元测试（零 renderdoc 依赖，即使没有 renderdoc 也能跑）
cmake -B build-unit -DBUILD_TESTING=ON   # 不设 RENDERDOC_DIR
cmake --build build-unit --config Release --target test-unit
ctest --test-dir build-unit -L unit

# 完整构建 + 所有测试（需 renderdoc 环境）
cmake -B build -DBUILD_TESTING=ON -DRENDERDOC_DIR=...
cmake --build build --config Release
ctest --test-dir build                   # 运行全部
ctest --test-dir build -L unit           # 仅纯单元
ctest --test-dir build -L integration    # 仅集成
```

## 测试 .rdc 文件

- 使用 Vulkan vkcube 示例的 RenderDoc 抓帧文件
- 文件：`tests/fixtures/vkcube.rdc`（134KB）
- API：Vulkan
- 包含：draw call、VS + PS shader、render target
- 当 renderdoc 版本升级导致 .rdc 格式变化时需重新抓取

## 测试输出文件处理

- export 相关测试（export_render_target、export_texture、export_buffer）会产生输出文件
- 测试使用临时目录存放导出文件，测试完成后清理
- CI 中不上传这些 artifact（如需调试失败用例，可手动本地重现）

## 不在本设计范围内

- Mock renderdoc API（使用真实文件）
- 代码覆盖率工具（后续增量添加）
- 性能/压力测试
- 跨平台支持（当前仅 Windows/MSVC）
