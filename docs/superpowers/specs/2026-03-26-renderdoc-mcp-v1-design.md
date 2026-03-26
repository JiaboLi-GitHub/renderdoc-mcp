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

- **参数**：`index?: number`（RT 索引，默认 0）
- **功能**：导出当前事件的渲染目标为 PNG 文件
- **输出路径**：服务器自动生成到**固定的临时目录**（`<capture所在目录>/renderdoc-mcp-export/`），文件名格式 `rt_<eventId>_<index>.png`。不接受用户指定路径，避免任意文件写入风险
- **返回**：输出文件路径、图片尺寸

## 会话模型

- 同一时间只支持一个 capture 会话（单 `IReplayController` 实例）
- `open_capture` 会关闭之前已打开的 capture
- 当前 eventId、controller 指针等状态保持在 `renderdoc_wrapper` 中

## MCP 协议层

### 传输

- **stdin/stdout**，消息格式：**换行分隔的 JSON**（每条 JSON-RPC 消息占一行，以 `\n` 分隔，消息内部不得包含换行）
- 这是 MCP 官方规范要求的 stdio 传输格式（注意：不是 LSP 的 Content-Length 分帧）
- 日志/调试输出写到 stderr
- stdout 只输出合法 MCP 消息，不得混入其他内容

### 支持的方法

| 方法 | 说明 |
|------|------|
| `initialize` | 返回 server info 和 capabilities（声明 tools） |
| `notifications/initialized` | 客户端确认，无需响应 |
| `tools/list` | 返回 5 个工具的 JSON Schema 定义 |
| `tools/call` | 分发到对应工具执行，返回结果 |

### MCP 协议版本

目标兼容 MCP protocol version `2025-03-26`。在 `initialize` 响应的 `protocolVersion` 字段中声明。如客户端协商更高版本则回退到本服务器支持的最高版本。

### 错误处理

**工具级错误**（tools/call 返回）：按 MCP 规范使用 `result.content[]` + `isError` 格式：

```json
{
  "content": [{ "type": "text", "text": "错误描述信息" }],
  "isError": true
}
```

适用场景：
- renderdoc API 失败（`ResultDetails.code != Succeeded`）→ 在 content text 中包含 `result.message`
- 未打开 capture 就调用其他工具 → 提示先调用 open_capture

**协议级错误**（JSON-RPC error response）：
- JSON 解析失败 → `-32700` (Parse error)
- 未知方法 → `-32601` (Method not found)
- 未知工具名 / 非法参数 → `-32602` (Invalid params)

### JSON-RPC Batch 支持

MCP 2025-03-26 要求：实现**必须支持接收** JSON-RPC batch（JSON 数组），**可选支持发送** batch。

本服务器的处理策略：
- **接收 batch**：消息循环检测到输入为 JSON 数组时，依次处理数组中的每个请求/通知，将所有响应收集到一个 JSON 数组中一次性返回
- **initialize 禁止出现在 batch 中**：如果 batch 数组中包含 `initialize` 请求，服务器拒绝整个 batch 并返回 JSON-RPC error（`-32600` Invalid Request），因为 MCP 规范要求 `initialize` 必须作为独立的单条请求发送
- **Notification 不产生响应**：batch 中的 notification 正常处理但不在响应数组中产生条目
- **全部为 notification 的 batch**：不返回任何内容（符合 JSON-RPC 2.0 规范）
- **发送 batch**：本服务器不主动发送 batch，所有响应以单消息或 batch 响应形式返回

## renderdoc 集成

### 链接方式

动态链接 `renderdoc.dll`。CMake 中通过两个变量指定 renderdoc 路径：
- `RENDERDOC_DIR`（必填）：renderdoc 源码根目录，提供头文件（`renderdoc/api/replay/` 下）
- `RENDERDOC_BUILD_DIR`（可选）：renderdoc 构建输出目录，提供 `renderdoc.lib` 和 `renderdoc.dll`。未设置时自动在 `RENDERDOC_DIR` 下的常见构建路径中搜索

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
  const SDFile &structuredFile = controller->GetStructuredFile();
  // 递归遍历 ActionDescription 树
  // 用 action.GetName(structuredFile) 获取名称（customName 多数情况为空，GetName 会回退到 chunk name）
  // 提取 action.eventId, action.GetName(structuredFile), action.flags

goto_event:
  controller->SetFrameEvent(eventId, true);

get_pipeline_state:
  // 根据 API 类型分支，只调用对应的 getter（返回指针，非引用）
  GraphicsAPI api = controller->GetAPIProperties().pipelineType;
  switch(api) {
    case GraphicsAPI::D3D11: {
      const D3D11Pipe::State *state = controller->GetD3D11PipelineState();
      // 提取 state->vertexShader, state->pixelShader, state->outputMerger 等
      break;
    }
    case GraphicsAPI::D3D12: {
      const D3D12Pipe::State *state = controller->GetD3D12PipelineState();
      break;
    }
    case GraphicsAPI::OpenGL: {
      const GLPipe::State *state = controller->GetGLPipelineState();
      break;
    }
    case GraphicsAPI::Vulkan: {
      const VKPipe::State *state = controller->GetVulkanPipelineState();  // 注意：是 GetVulkanPipelineState，不是 GetVK...
      break;
    }
  }

export_render_target:
  // 1. 从管线状态获取当前 RT 的 ResourceId
  // 2. 使用 SaveTexture 直接保存（保留 alpha 通道，利用 renderdoc 内置格式处理）
  TextureSave saveData = {};
  saveData.resourceId = rtResourceId;
  saveData.destType = FileType::PNG;    // 保存为 PNG，保留 RGBA
  rdcstr outputPath = generateOutputPath(eventId, index);  // 自动生成到 export 目录
  ResultDetails result = controller->SaveTexture(saveData, outputPath);
  // 必须检查 result.code == ResultCode::Succeeded
```

### 注意事项

- **REPLAY_PROGRAM_MARKER**：`main.cpp` 中必须包含 `REPLAY_PROGRAM_MARKER()` 宏，防止 renderdoc 尝试捕获 MCP 服务器自身
- **单线程设计**：renderdoc replay API 有线程约束，所有 replay 调用在主线程（stdio 消息循环线程）上顺序执行
- **ResultDetails 检查**：`OpenFile`、`OpenCapture`、`SaveTexture` 的返回值必须检查，失败时返回 MCP tool error（content[] + isError 格式）
- **SaveTexture 替代 ReadbackOutputTexture**：使用 `IReplayController::SaveTexture(TextureSave, path)` 直接保存，保留 alpha 通道和 renderdoc 内置的格式语义处理，不再需要手动 readback + stb_image_write

### 资源生命周期

- `IReplayController` 和 `ICaptureFile`：`open_capture` 时创建，下次 `open_capture` 或进程退出时释放
- `SaveTexture` 无需额外资源管理，直接调用即可
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
└── README.md
```

## 依赖

| 依赖 | 方式 | 说明 |
|------|------|------|
| renderdoc | 动态链接 .dll/.lib | 用户通过 `RENDERDOC_DIR` CMake 变量指定 |
| nlohmann/json | CMake FetchContent | header-only JSON 库 |

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
