# renderdoc-mcp

[English](README.md) | **中文**

为 AI 提供 GPU 渲染调试能力的 MCP (Model Context Protocol) 服务器。让 Claude、Codex 等 AI 助手能够通过标准 MCP 接口分析 RenderDoc 抓帧文件 (.rdc)。

## 功能

- **20 个 MCP 工具**，覆盖完整 GPU 调试工作流
- 打开和分析 `.rdc` 抓帧文件（D3D11 / D3D12 / OpenGL / Vulkan）
- 列出 draw call、事件、渲染 pass，支持过滤
- 查看管线状态、shader 绑定、资源详情
- 获取 shader 反汇编和反射数据，跨 shader 文本搜索
- 导出纹理、buffer、渲染目标为 PNG/二进制文件
- 性能统计、调试/验证日志消息
- 自动参数校验，返回标准 JSON-RPC 错误码

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

## 工具列表（20 个）

### 会话管理

| 工具 | 说明 |
|------|------|
| `open_capture` | 打开 `.rdc` 文件进行分析，返回 API 类型和事件总数 |

### 事件与 Draw Call

| 工具 | 说明 |
|------|------|
| `list_events` | 列出所有事件，支持名称过滤 |
| `goto_event` | 跳转到指定事件 |
| `list_draws` | 列出 draw call，含顶点/索引数、实例数 |
| `get_draw_info` | 获取单个 draw call 的详细信息 |

### 管线与绑定

| 工具 | 说明 |
|------|------|
| `get_pipeline_state` | 获取管线状态（shader、RT、viewport），支持可选 `eventId` 参数 |
| `get_bindings` | 获取各 shader stage 的资源绑定（CBV/SRV/UAV/Sampler） |

### Shader

| 工具 | 说明 |
|------|------|
| `get_shader` | 获取指定 stage 的 shader 反汇编或反射数据（`vs`/`hs`/`ds`/`gs`/`ps`/`cs`） |
| `list_shaders` | 列出所有唯一 shader 及其 stage 和使用次数 |
| `search_shaders` | 在 shader 反汇编文本中搜索模式 |

### 资源

| 工具 | 说明 |
|------|------|
| `list_resources` | 列出所有 GPU 资源，支持类型/名称过滤 |
| `get_resource_info` | 获取资源详情（格式、尺寸、字节大小） |
| `list_passes` | 列出渲染 pass（含 draw call 的 marker 区域） |
| `get_pass_info` | 获取 pass 详情，包含其中的 draw call 列表 |

### 导出

| 工具 | 说明 |
|------|------|
| `export_render_target` | 导出当前事件的渲染目标为 PNG |
| `export_texture` | 按资源 ID 导出任意纹理为 PNG |
| `export_buffer` | 导出 buffer 数据为二进制文件 |

### 信息与诊断

| 工具 | 说明 |
|------|------|
| `get_capture_info` | 获取抓帧元数据：API、GPU、驱动、事件统计 |
| `get_stats` | 性能统计：per-pass 分解、top draw、最大资源 |
| `get_log` | 调试/验证消息，支持严重级别和事件 ID 过滤 |

## 工具详情

### open_capture

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|-----|------|
| `path` | string | 是 | `.rdc` 文件的绝对路径 |

**响应：**
```json
{ "api": "D3D12", "eventCount": 1247 }
```

---

### list_draws

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|-----|------|
| `filter` | string | 否 | 按名称关键字过滤 |
| `limit` | integer | 否 | 最大结果数（默认 1000） |

**响应：**
```json
{
  "draws": [
    { "eventId": 42, "name": "DrawIndexed(360)", "flags": "Drawcall|Indexed", "numIndices": 360, "numInstances": 1, "drawIndex": 0 }
  ],
  "count": 1
}
```

---

### get_shader

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|-----|------|
| `stage` | string | 是 | shader stage：`vs`、`hs`、`ds`、`gs`、`ps`、`cs` |
| `eventId` | integer | 否 | 事件 ID（省略则使用当前事件） |
| `mode` | string | 否 | `disasm`（默认）或 `reflect` |

---

### get_pipeline_state

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|-----|------|
| `eventId` | integer | 否 | 要查看的事件 ID（省略则使用当前事件） |

**响应示例（D3D12）：**
```json
{
  "api": "D3D12",
  "eventId": 42,
  "vertexShader": { "resourceId": "ResourceId::15", "entryPoint": "VSMain" },
  "pixelShader": { "resourceId": "ResourceId::16", "entryPoint": "PSMain" },
  "renderTargets": [{ "index": 0, "resourceId": "ResourceId::7", "format": "R8G8B8A8_UNORM" }],
  "viewports": [{ "x": 0.0, "y": 0.0, "width": 1920.0, "height": 1080.0 }]
}
```

> OpenGL 和 Vulkan 抓帧中，像素着色器的键名为 `fragmentShader`。

---

### get_bindings

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|-----|------|
| `eventId` | integer | 否 | 事件 ID（省略则使用当前事件） |

**响应：**
```json
{
  "api": "D3D12",
  "stages": {
    "vs": {
      "shader": "ResourceId::15",
      "bindings": {
        "constantBuffers": [{ "name": "CBScene", "bindPoint": 0, "byteSize": 256 }],
        "readOnlyResources": [{ "name": "diffuseMap", "bindPoint": 0 }]
      }
    }
  }
}
```

---

### list_resources

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|-----|------|
| `type` | string | 否 | 按类型过滤：`Texture`、`Buffer`、`Shader` 等 |
| `name` | string | 否 | 按名称关键字过滤 |

---

### export_texture

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|-----|------|
| `resourceId` | string | 是 | 资源 ID（如 `ResourceId::123`） |
| `mip` | integer | 否 | Mip 级别（默认 0） |
| `layer` | integer | 否 | 数组层（默认 0） |

---

### export_buffer

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|-----|------|
| `resourceId` | string | 是 | 资源 ID |
| `offset` | integer | 否 | 字节偏移（默认 0） |
| `size` | integer | 否 | 字节数，0 表示全部（默认 0） |

---

### get_log

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|-----|------|
| `level` | string | 否 | 最低严重级别：`HIGH`、`MEDIUM`、`LOW`、`INFO` |
| `eventId` | integer | 否 | 按事件 ID 过滤 |

## 典型工作流

```
1. open_capture        → 打开 .rdc 抓帧文件
2. get_capture_info    → 查看 API、GPU、事件统计
3. list_draws          → 找到感兴趣的 draw call
4. goto_event          → 跳转到该 draw call
5. get_pipeline_state  → 查看 shader、渲染目标、viewport
6. get_bindings        → 查看各 stage 的资源绑定
7. get_shader          → 读取 shader 反汇编或反射数据
8. export_render_target → 导出渲染目标为 PNG
9. get_log             → 检查验证错误
```

## 协议细节

| 属性 | 值 |
|------|-----|
| 传输方式 | stdio（stdin/stdout）|
| 消息格式 | 换行分隔的 JSON-RPC 2.0 |
| MCP 协议版本 | 2025-03-26 |
| Batch 支持 | 支持接收；`initialize` 禁止出现在 batch 中 |
| 日志输出 | stderr |

## 架构

```
AI 客户端 (Claude/Codex)
    |
    | stdin/stdout (JSON-RPC, 换行分隔)
    |
renderdoc-mcp.exe
    ├── McpServer        (协议层)
    ├── ToolRegistry     (工具注册 + 参数校验)
    ├── RenderdocWrapper  (会话状态管理)
    └── tools/*.cpp      (20 个工具实现)
    |
    | C++ 动态链接
    |
renderdoc.dll (Replay API)
```

单进程、单线程设计。同一时间只支持一个抓帧会话。ToolRegistry 提供自动 `inputSchema` 校验（必填字段、类型检查、枚举值校验），返回标准 JSON-RPC `-32602` 错误响应。

## 手动测试

```bash
# 初始化
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}' | renderdoc-mcp.exe

# 查询工具列表
echo '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | renderdoc-mcp.exe
```

## 许可证

本项目采用 [MIT 许可证](LICENSE)。

RenderDoc 本身有独立的许可证，请参阅 [renderdoc 许可证](https://github.com/baldurk/renderdoc/blob/v1.x/LICENSE.md)。
