# renderdoc-mcp

[English](README.md) | **中文**

为 AI 提供 GPU 渲染调试能力的 MCP (Model Context Protocol) 服务器。让 Claude、Codex 等 AI 助手能够通过标准 MCP 接口分析 RenderDoc 抓帧文件 (.rdc)。

## 功能

- 打开和分析 `.rdc` 抓帧文件
- 列出所有 draw call 和 GPU 事件，支持关键字过滤
- 跳转到指定事件，查看渲染管线状态
- 查询绑定的 shader、渲染目标、viewport（支持 D3D11 / D3D12 / OpenGL / Vulkan）
- 导出渲染目标为 PNG 图片

## 前置条件

- **Windows**（MSVC 编译器，C++17）
- **CMake** >= 3.16
- **RenderDoc** 源码及构建产物（`renderdoc.lib` + `renderdoc.dll`）

## 构建

```bash
# 基础用法（自动查找 renderdoc 构建产物）
cmake -B build -DRENDERDOC_DIR=D:/renderdoc/renderdoc
cmake --build build --config Release

# 手动指定构建输出目录
cmake -B build ^
  -DRENDERDOC_DIR=D:/renderdoc/renderdoc ^
  -DRENDERDOC_BUILD_DIR=D:/renderdoc/renderdoc/build
cmake --build build --config Release
```

### CMake 变量

| 变量 | 是否必需 | 说明 |
|------|---------|------|
| `RENDERDOC_DIR` | 是 | RenderDoc 源码根目录 |
| `RENDERDOC_BUILD_DIR` | 否 | RenderDoc 构建产物目录（不在标准位置时需指定） |

构建产物：`build/Release/renderdoc-mcp.exe`

需要确保 `renderdoc.dll` 与可执行文件在同一目录。CMake 会在找到 dll 时自动复制。

## 客户端配置

### Claude Code

在 Claude Code 的 MCP 配置文件（`settings.json`）中添加：

```json
{
  "mcpServers": {
    "renderdoc": {
      "command": "D:/renderdoc/renderdoc-mcp/build/Release/renderdoc-mcp.exe",
      "args": []
    }
  }
}
```

### Codex 及其他 MCP 客户端

任何支持 stdio 传输方式的 MCP 客户端都可以使用，只需指向可执行文件路径即可。

## 工具列表

### open_capture

打开 RenderDoc 抓帧文件进行分析。如果之前已打开其他抓帧，会自动关闭。

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|-----|------|
| `path` | string | 是 | `.rdc` 文件的绝对路径 |

**请求示例：**
```json
{
  "name": "open_capture",
  "arguments": {
    "path": "D:/captures/frame_001.rdc"
  }
}
```

**响应示例：**
```json
{
  "api": "D3D12",
  "eventCount": 1247
}
```

---

### list_events

列出当前抓帧中的所有 draw call 和事件。

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|-----|------|
| `filter` | string | 否 | 大小写不敏感的过滤关键字 |

**请求示例：**
```json
{
  "name": "list_events",
  "arguments": {
    "filter": "DrawIndexed"
  }
}
```

**响应示例：**
```json
{
  "events": [
    { "eventId": 42, "name": "DrawIndexed(360)", "flags": "Drawcall|Indexed" },
    { "eventId": 87, "name": "DrawIndexed(1200)", "flags": "Drawcall|Indexed|Instanced" }
  ],
  "count": 2
}
```

---

### goto_event

跳转到指定事件。之后调用 `get_pipeline_state` 和 `export_render_target` 都会基于此事件。

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|-----|------|
| `eventId` | integer | 是 | 目标事件 ID |

**请求示例：**
```json
{
  "name": "goto_event",
  "arguments": {
    "eventId": 42
  }
}
```

---

### get_pipeline_state

获取当前事件的图形管线状态。返回绑定的 shader、渲染目标、viewport 等信息。需先调用 `goto_event`。

**参数：** 无

**请求示例：**
```json
{
  "name": "get_pipeline_state",
  "arguments": {}
}
```

**响应示例（D3D12）：**
```json
{
  "api": "D3D12",
  "eventId": 42,
  "vertexShader": {
    "resourceId": "ResourceId::15",
    "entryPoint": "VSMain"
  },
  "pixelShader": {
    "resourceId": "ResourceId::16",
    "entryPoint": "PSMain"
  },
  "renderTargets": [
    { "index": 0, "resourceId": "ResourceId::7", "format": "R8G8B8A8_UNORM" }
  ],
  "viewports": [
    { "x": 0.0, "y": 0.0, "width": 1920.0, "height": 1080.0 }
  ]
}
```

> OpenGL 和 Vulkan 抓帧中，像素着色器的键名为 `fragmentShader`。

---

### export_render_target

将当前事件的渲染目标导出为 PNG 文件。输出路径由服务器自动生成。

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|-----|------|
| `index` | integer | 否 | 渲染目标索引 (0-7)，默认 0 |

**请求示例：**
```json
{
  "name": "export_render_target",
  "arguments": {
    "index": 0
  }
}
```

**响应示例：**
```json
{
  "path": "D:/captures/renderdoc-mcp-export/rt_42_0.png",
  "width": 1920,
  "height": 1080,
  "eventId": 42,
  "rtIndex": 0
}
```

输出文件保存在 `<抓帧文件所在目录>/renderdoc-mcp-export/` 下。

## 典型工作流

```
1. open_capture        → 打开 .rdc 抓帧文件
2. list_events         → 浏览事件列表，找到目标 draw call
3. goto_event          → 跳转到该事件
4. get_pipeline_state  → 查看 shader、渲染目标、viewport 等管线状态
5. export_render_target → 导出渲染目标为 PNG 进行可视化检查
```

## 协议细节

| 属性 | 值 |
|------|-----|
| 传输方式 | stdio（stdin/stdout）|
| 消息格式 | 换行分隔的 JSON-RPC 2.0 |
| MCP 协议版本 | 2025-03-26 |
| Batch 支持 | 支持接收；`initialize` 禁止出现在 batch 中 |
| 日志输出 | stderr |

## 手动测试

```bash
# 初始化
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}' | renderdoc-mcp.exe

# 查询工具列表
echo '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | renderdoc-mcp.exe
```

## 架构

```
AI 客户端 (Claude/Codex)
    |
    | stdin/stdout (JSON-RPC, 换行分隔)
    |
renderdoc-mcp.exe
    |
    | C++ 动态链接
    |
renderdoc.dll (Replay API)
```

单进程、单线程设计。同一时间只支持一个抓帧会话。

## 许可证

RenderDoc 组件请参阅 [renderdoc 许可证](https://github.com/baldurk/renderdoc/blob/v1.x/LICENSE.md)。
