# renderdoc-mcp v1 实现计划

## Context

根据已审批通过的设计文档 `docs/superpowers/specs/2026-03-26-renderdoc-mcp-v1-design.md`，实现 renderdoc-mcp 的第一个可工作版本。项目目录 `D:/renderdoc/renderdoc-mcp/` 当前只有设计文档，需要从零搭建 CMake 工程和全部源码。

参考实现：`D:/renderdoc/rdc-cli/`（Python），提供了 API 用法参考。

---

## Step 1: CMakeLists.txt

**文件**: `D:/renderdoc/renderdoc-mcp/CMakeLists.txt`

- `cmake_minimum_required(VERSION 3.16)`，`project(renderdoc-mcp)`，`set(CMAKE_CXX_STANDARD 17)`
- `RENDERDOC_DIR` cache 变量（默认 `D:/renderdoc/renderdoc`）
- FetchContent 引入 nlohmann/json v3.11.3
- `add_executable(renderdoc-mcp ...)` 包含所有 src 文件
- include dirs: `${RENDERDOC_DIR}/renderdoc/api/replay` 和 renderdoc 其他必要头文件路径
- link: `renderdoc.lib`（从 renderdoc 构建输出目录）
- 安装/拷贝 `renderdoc.dll` 到输出目录（post-build command）

---

## Step 2: main.cpp

**文件**: `D:/renderdoc/renderdoc-mcp/src/main.cpp`

- `REPLAY_PROGRAM_MARKER()` 宏（防止自捕获）
- `main()` 函数：
  1. 设置 stdin/stdout 为二进制模式（Windows: `_setmode`）
  2. 创建 `McpServer` 实例
  3. 消息循环：逐行读取 stdin（`std::getline`），解析 JSON
    - 如果是 JSON 数组 → batch 处理（检查 initialize 禁入 batch）
    - 如果是 JSON 对象 → 单条处理
  4. 将响应序列化为单行 JSON + `\n` 写入 stdout
  5. 退出时调用 cleanup

---

## Step 3: mcp_server.h / mcp_server.cpp

**文件**: `D:/renderdoc/renderdoc-mcp/src/mcp_server.h`，`D:/renderdoc/renderdoc-mcp/src/mcp_server.cpp`

**McpServer 类**：
- 成员：`RenderdocWrapper m_wrapper`，`bool m_initialized`
- `json handleMessage(const json& msg)` — 分发单条消息：
  - `initialize` → 返回 serverInfo + capabilities（tools）+ protocolVersion `2025-03-26`
  - `notifications/initialized` → 设置 `m_initialized = true`，无响应
  - `tools/list` → 返回 5 个工具的 name + description + inputSchema
  - `tools/call` → 根据 `params.name` 分发到对应工具函数
  - 其他方法 → JSON-RPC error `-32601`
- `json handleBatch(const json& arr)` — batch 处理：
  - 扫描是否含 `initialize`，如含则返回 `-32600` 拒绝
  - 遍历处理，收集响应数组
- 工具 schema 定义：每个工具的 JSON Schema（inputSchema）

---

## Step 4: renderdoc_wrapper.h / renderdoc_wrapper.cpp

**文件**: `D:/renderdoc/renderdoc-mcp/src/renderdoc_wrapper.h`，`D:/renderdoc/renderdoc-mcp/src/renderdoc_wrapper.cpp`

**RenderdocWrapper 类**：
- 成员状态：
  - `ICaptureFile* m_captureFile = nullptr`
  - `IReplayController* m_controller = nullptr`
  - `uint32_t m_currentEventId = 0`
  - `std::string m_capturePath`（用于生成 export 目录）
  - `bool m_replayInitialized = false`
- 方法：
  - `initReplay()` — 首次调用 `RENDERDOC_InitialiseReplay`
  - `openCapture(path)` → 返回 `{api, eventCount}` 或错误
  - `listEvents(filter)` → 递归遍历 `GetRootActions()`，使用 `GetName(GetStructuredFile())` 取名称
  - `gotoEvent(eventId)` → `SetFrameEvent`
  - `getPipelineState()` → 根据 `GetAPIProperties().pipelineType` 分支，提取关键字段
  - `exportRenderTarget(index)` → 从管线状态获取 RT ResourceId，调用 `SaveTexture`
  - `shutdown()` — 释放 controller/file，调用 `RENDERDOC_ShutdownReplay`
- 辅助：
  - `getExportDir()` — 返回 `<capture所在目录>/renderdoc-mcp-export/`
  - `generateOutputPath(eventId, index)` — `rt_<eventId>_<index>.png`
  - `flattenActions(actions, structuredFile, filter)` — 递归遍历 action 树

---

## Step 5: tools/*.cpp（5 个工具实现）

每个文件是一个独立函数，由 `McpServer::tools/call` 分发调用。

### 5a. `open_capture.cpp`
- 解析 `params.arguments.path`
- 调用 `m_wrapper.openCapture(path)`
- 返回 `content: [{type: "text", text: JSON{api, eventCount}}]`

### 5b. `list_events.cpp`
- 解析可选 `params.arguments.filter`
- 调用 `m_wrapper.listEvents(filter)`
- 返回事件列表 JSON（eventId, name, flags 字符串表示）

### 5c. `goto_event.cpp`
- 解析 `params.arguments.eventId`
- 调用 `m_wrapper.gotoEvent(eventId)`
- 返回当前事件信息

### 5d. `get_pipeline_state.cpp`
- 调用 `m_wrapper.getPipelineState()`
- 根据 API 类型提取并返回：
  - Vertex Shader / Pixel Shader：resourceId、entryPoint
  - Render Targets：resourceId、format
  - Viewport / Scissor
  - Blend state（简化）

### 5e. `export_render_target.cpp`
- 解析可选 `params.arguments.index`（默认 0）
- 调用 `m_wrapper.exportRenderTarget(index)`
- 返回 `{path, width, height}`

---

## Step 6: 构建验证

1. CMake configure + build：
   ```bash
   cd D:/renderdoc/renderdoc-mcp
   cmake -B build -DRENDERDOC_DIR=D:/renderdoc/renderdoc
   cmake --build build --config Release
   ```
2. 确保 `renderdoc.dll` 在可执行文件旁边

---

## Step 7: 端到端测试

用 echo 向 stdin 发送 MCP 消息，验证响应：

```bash
# 1. initialize
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}' | renderdoc-mcp.exe

# 2. tools/list
echo '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | renderdoc-mcp.exe

# 3. open_capture + list_events + goto_event + get_pipeline_state + export_render_target
# （需要实际 .rdc 文件）
```

---

## 实现顺序

1. **CMakeLists.txt** — 先确保能编译空项目
2. **renderdoc_wrapper** — 核心 renderdoc 封装，可独立编译测试
3. **mcp_server** — MCP 协议层
4. **main.cpp** — stdio 消息循环
5. **tools/\*** — 5 个工具函数
6. **构建 + 端到端测试**

## 关键文件引用

| 用途 | 路径 |
|------|------|
| 设计文档 | `D:/renderdoc/renderdoc-mcp/docs/superpowers/specs/2026-03-26-renderdoc-mcp-v1-design.md` |
| renderdoc 主接口 | `D:/renderdoc/renderdoc/renderdoc/api/replay/renderdoc_replay.h` |
| 数据类型 | `D:/renderdoc/renderdoc/renderdoc/api/replay/data_types.h` |
| 控制类型 | `D:/renderdoc/renderdoc/renderdoc/api/replay/control_types.h` |
| 枚举定义 | `D:/renderdoc/renderdoc/renderdoc/api/replay/replay_enums.h` |
| 管线状态（D3D11） | `D:/renderdoc/renderdoc/renderdoc/api/replay/d3d11_pipestate.h` |
| 管线状态（D3D12） | `D:/renderdoc/renderdoc/renderdoc/api/replay/d3d12_pipestate.h` |
| 管线状态（GL） | `D:/renderdoc/renderdoc/renderdoc/api/replay/gl_pipestate.h` |
| 管线状态（VK） | `D:/renderdoc/renderdoc/renderdoc/api/replay/vk_pipestate.h` |
| ResourceId | `D:/renderdoc/renderdoc/renderdoc/api/replay/resourceid.h` |
| Python 参考（adapter） | `D:/renderdoc/rdc-cli/src/rdc/adapter.py` |
| Python 参考（texture） | `D:/renderdoc/rdc-cli/src/rdc/handlers/texture.py` |
