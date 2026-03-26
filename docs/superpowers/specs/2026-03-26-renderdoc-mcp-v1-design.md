# renderdoc-mcp v1 设计文档

## 概述

为 AI（Claude、Codex 等）提供 GPU 渲染调试能力的 MCP 服务器。通过 C++ 直接调用 renderdoc 的 Replay API，以 stdio 方式本地部署。

## 架构

**方案：单进程直接链接**

```
Claude/Codex ←→ stdio (JSON-RPC) ←→ renderdoc-mcp.exe ←→ renderdoc.dll
```

单个可执行文件，直接动态链接 `renderdoc.dll`，通过 stdin/stdout 与 AI 客户端通信。

### 组件

| 组件 | 职责 |
|------|------|
| `main.cpp` | 入口，stdio 消息循环（读 stdin、写 stdout） |
| `mcp_server` | MCP 协议解析与方法分发 |
| `renderdoc_wrapper` | 封装 renderdoc C API，管理会话状态 |
| `tools/*` | 每个 MCP tool 的具体实现 |

## MCP Tools（5 个，最小闭环）

### 1. open_capture

- **参数**：`path: string` — .rdc 文件路径
- **功能**：打开 capture 文件，初始化 ReplayController
- **返回**：API 类型（D3D11/D3D12/GL/VK）、事件总数
- **副作用**：关闭之前已打开的 capture

### 2. list_events

- **参数**：`filter?: string` — 可选过滤关键字
- **功能**：列出所有 draw call / action 事件
- **返回**：事件列表（eventId、名称、flags）

### 3. goto_event

- **参数**：`eventId: number`
- **功能**：跳转到指定事件
- **返回**：该事件的基本信息

### 4. get_pipeline_state

- **参数**：无（使用当前事件）
- **功能**：获取当前事件的管线状态
- **返回**：VS/PS shader info、bound render targets、viewport、scissor、blend state 等

### 5. export_render_target

- **参数**：`outputPath: string`、`index?: number`（RT 索引，默认 0）
- **功能**：导出当前事件的渲染目标为 PNG 文件
- **返回**：输出文件路径、图片尺寸

## 会话模型

- 同一时间只支持一个 capture 会话（单 `IReplayController` 实例）
- `open_capture` 会关闭之前已打开的 capture
- 当前 eventId、controller 指针等状态保持在 `renderdoc_wrapper` 中

## MCP 协议层

### 传输

- **stdin/stdout**，消息格式：`Content-Length: <n>\r\n\r\n<JSON-RPC body>`
- 日志/调试输出写到 stderr

### 支持的方法

| 方法 | 说明 |
|------|------|
| `initialize` | 返回 server info 和 capabilities（声明 tools） |
| `notifications/initialized` | 客户端确认，无需响应 |
| `tools/list` | 返回 5 个工具的 JSON Schema 定义 |
| `tools/call` | 分发到对应工具执行，返回结果 |

### MCP 协议版本

实现 MCP protocol version `2024-11-05`（当前稳定版），在 `initialize` 响应中声明。

### 错误处理

- renderdoc API 失败（`ResultDetails.code != Succeeded`）→ MCP tool result `isError: true` + `result.message` 错误描述
- 未打开 capture 就调用其他工具 → MCP tool result `isError: true` + 提示先调用 open_capture
- JSON 解析失败 → JSON-RPC error `-32700`
- 未知方法 → JSON-RPC error `-32601`

## renderdoc 集成

### 链接方式

动态链接 `renderdoc.dll`。CMake 中通过 `RENDERDOC_DIR` 变量指向 renderdoc 构建输出目录。

### API 调用流程

```cpp
open_capture:
  // 首次调用时初始化（GlobalEnvironment 可默认构造，args 传空）
  GlobalEnvironment env = {};
  RENDERDOC_InitialiseReplay(env, {});

  ICaptureFile* file = RENDERDOC_OpenCaptureFile();
  ResultDetails result = file->OpenFile(path, "", nullptr);  // 第三参数为进度回调，可传 nullptr
  // 必须检查 result.code == ResultCode::Succeeded

  ReplayOptions opts;
  auto [openResult, controller] = file->OpenCapture(opts, nullptr);  // 返回 pair，非 out 参数
  // 必须检查 openResult.code == ResultCode::Succeeded

list_events:
  rdcarray<ActionDescription> actions = controller->GetRootActions();
  // 递归遍历 ActionDescription 树
  // 提取 action.eventId, action.customName, action.flags

goto_event:
  controller->SetFrameEvent(eventId, true);

get_pipeline_state:
  // 使用 API 特定的管线状态接口（根据 GetAPIProperties().pipelineType）
  const D3D11Pipe::State& d3d11 = controller->GetD3D11PipelineState();
  const D3D12Pipe::State& d3d12 = controller->GetD3D12PipelineState();
  const GLPipe::State& gl = controller->GetGLPipelineState();
  const VKPipe::State& vk = controller->GetVKPipelineState();
  // 从对应状态中提取 VS/PS shader、render targets、viewport 等

export_render_target:
  // 1. 从管线状态获取当前 RT 的 ResourceId
  // 2. 创建无窗口输出（headless mode）
  WindowingData headless = CreateHeadlessWindowingData(width, height);
  IReplayOutput* output = controller->CreateOutput(headless, ReplayOutputType::Texture);
  // 3. 配置 TextureDisplay，设置 resourceId 为目标 RT
  TextureDisplay display = {};
  display.resourceId = rtResourceId;
  output->SetTextureDisplay(display);
  // 4. 读回像素数据（注意：返回的是 RGB 3字节紧密排列，非 RGBA）
  bytebuf pixels = output->ReadbackOutputTexture();
  // 5. 用 stbi_write_png 写入，comp=3（RGB）
  output->Shutdown();
```

### 注意事项

- **REPLAY_PROGRAM_MARKER**：`main.cpp` 中必须包含 `REPLAY_PROGRAM_MARKER()` 宏，防止 renderdoc 尝试捕获 MCP 服务器自身
- **单线程设计**：renderdoc replay API 有线程约束，所有 replay 调用在主线程（stdio 消息循环线程）上顺序执行
- **ResultDetails 检查**：`OpenFile` 和 `OpenCapture` 的返回值必须检查，失败时返回 MCP tool error 并附带 `result.message`
- **像素格式**：`ReadbackOutputTexture` 返回 RGB 3字节数据，写 PNG 时 `comp=3`

### 资源生命周期

- `IReplayController` 和 `ICaptureFile`：`open_capture` 时创建，下次 `open_capture` 或进程退出时释放
- `IReplayOutput`：`export_render_target` 时临时创建，调用后 `Shutdown()` 释放
- `RENDERDOC_ShutdownReplay()`：进程退出时调用

## 项目结构

```
renderdoc-mcp/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── mcp_server.h / .cpp
│   ├── renderdoc_wrapper.h / .cpp
│   └── tools/
│       ├── open_capture.cpp
│       ├── list_events.cpp
│       ├── goto_event.cpp
│       ├── get_pipeline_state.cpp
│       └── export_render_target.cpp
├── third_party/
│   └── stb_image_write.h
└── README.md
```

## 依赖

| 依赖 | 方式 | 说明 |
|------|------|------|
| renderdoc | 动态链接 .dll/.lib | 用户通过 `RENDERDOC_DIR` CMake 变量指定 |
| nlohmann/json | CMake FetchContent | header-only JSON 库 |
| stb_image_write | third_party/ 目录 | header-only PNG 编码 |

## 构建

```bash
cmake -B build -DRENDERDOC_DIR=D:/renderdoc/renderdoc
cmake --build build --config Release
```

编译器要求：MSVC，C++17。

## AI 客户端配置

### Claude Code

```json
{
  "mcpServers": {
    "renderdoc": {
      "command": "path/to/renderdoc-mcp.exe",
      "args": []
    }
  }
}
```

### Codex 及其他 MCP 客户端

类似的 stdio 配置，指定可执行文件路径即可。

## 未来演进

- **v2**：增加 shader 源码查看、资源列表、buffer 数据读取
- **稳定性**：演进为双进程架构（MCP server + replay worker），隔离崩溃
- **远程调试**：支持 renderdoc 的 RemoteServer 接口
