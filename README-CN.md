# renderdoc-mcp

[English](README.md) | **中文**

为 AI 提供 GPU 渲染调试能力的 MCP (Model Context Protocol) 服务器。让 Claude、Codex 等 AI 助手能够通过标准 MCP 接口分析 RenderDoc 抓帧文件 (`.rdc`)。

## 功能

- **21 个 MCP 工具**，覆盖完整的 GPU 调试工作流
- **CLI 工具**（`renderdoc-cli`），适用于脚本和 Shell 工作流
- 打开并分析 `.rdc` 抓帧文件（D3D11 / D3D12 / OpenGL / Vulkan）
- **实时抓帧**：启动应用程序并注入 RenderDoc，抓取一帧后自动打开分析
- 列出 draw call、事件、渲染 pass，并支持过滤
- 查看管线状态、shader 绑定和资源详情
- 获取 shader 反汇编与反射数据，并支持跨 shader 搜索
- 导出纹理、buffer 和渲染目标为 PNG 或二进制文件
- 查看性能统计和调试/验证日志
- 自动执行参数校验，并返回标准 JSON-RPC 错误码
- **分层架构**：核心库被 MCP 服务器、CLI 和 AI skill 共享

## 前置条件

- **Windows**（MSVC 编译器，C++17）
- **CMake** >= 3.16
- **RenderDoc** 源码及构建产物（`renderdoc.lib` + `renderdoc.dll`）

## 下载

预编译的 Windows x64 二进制文件可以从 [GitHub Releases](https://github.com/JiaboLi-GitHub/renderdoc-mcp/releases) 页面下载。

每个发布包 zip 中包含：

- `bin/renderdoc-mcp.exe` — MCP 服务器
- `bin/renderdoc-cli.exe` — 命令行工具
- `bin/renderdoc.dll` 以及程序运行所需的额外 RenderDoc 运行时 DLL
- `skills/renderdoc-mcp/` — Codex skill
- `install-codex.ps1` — 用于把 MCP、skill 和 CLI 安装到 Codex
- `README.md`、`README-CN.md` 和许可证文件

解压后请保持发布包目录结构不变。

从 `v0.2.1` 开始，解压后的发布包会把可执行文件放在 `bin/` 下，把 Codex skill 放在 `skills/renderdoc-mcp/` 下，并额外提供 `install-codex.ps1` 来完成 Codex 安装。

## 在 Codex 中安装

在解压后的发布包目录中运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\install-codex.ps1
```

安装脚本会：

- 把发布包安装到 `~/.codex/vendor_imports/renderdoc-mcp`
- 在 `~/.codex/config.toml` 中注册名为 `renderdoc-mcp` 的 MCP 服务器
- 把 skill 安装到 `~/.codex/skills/renderdoc-mcp`
- 把发布包中的 `bin/` 目录加入用户级 `PATH`，这样 `renderdoc-cli` 可以在 Shell 和 skill 中直接使用

安装完成后请重启 Codex Desktop。

## 构建

```bash
# 基础用法（自动查找 renderdoc 构建输出）
cmake -B build -DRENDERDOC_DIR=D:/renderdoc/renderdoc
cmake --build build --config Release

# 显式指定构建输出目录
cmake -B build \
  -DRENDERDOC_DIR=D:/renderdoc/renderdoc \
  -DRENDERDOC_BUILD_DIR=D:/renderdoc/renderdoc/build
cmake --build build --config Release
```

### CMake 变量

| 变量 | 是否必需 | 说明 |
|------|----------|------|
| `RENDERDOC_DIR` | 是 | RenderDoc 源码根目录 |
| `RENDERDOC_BUILD_DIR` | 否 | RenderDoc 构建输出目录（不在标准位置时需要指定） |

构建产物：`build/Release/renderdoc-mcp.exe` 和 `build/Release/renderdoc-cli.exe`

请确保 `renderdoc.dll` 与可执行文件位于同一目录。若 CMake 找到该文件，会自动将其复制到输出目录。

## 客户端配置

### Claude Code

在 Claude Code 的 MCP 配置文件（`settings.json`）中添加：

```json
{
  "mcpServers": {
    "renderdoc-mcp": {
      "command": "C:/Users/your-user/.codex/vendor_imports/renderdoc-mcp/bin/renderdoc-mcp.exe",
      "args": []
    }
  }
}
```

### Codex 及其他 MCP 客户端

对于 Codex，发布包安装脚本会向 `~/.codex/config.toml` 写入如下配置：

```toml
[mcp_servers.renderdoc-mcp]
command = 'C:\Users\your-user\.codex\vendor_imports\renderdoc-mcp\bin\renderdoc-mcp.exe'
args = []
```

任何支持 stdio 传输方式的 MCP 客户端都可以使用 renderdoc-mcp，只需将命令路径指向发布包中的 `bin/renderdoc-mcp.exe` 即可。

## 效果演示

使用仓库内置的 `tests/fixtures/vkcube.rdc` 样例抓帧，下面这段更接近真实使用方式: 用户直接提问，AI 再通过 renderdoc-mcp 去读抓帧并总结结果。

```text
用户: vkcube.rdc 包含哪些信息？

AI: 我通过 renderdoc-mcp 检查了这个抓帧，先给你一个快速总结：

- 这是一个 Vulkan 抓帧，共有 6 个事件、1 个 draw call。
- 主要 draw call 是事件 11 的 `vkCmdDraw()`，绘制了 36 个索引，实例数为 1。
- 在这个事件上，管线使用了顶点着色器 `ResourceId::111` 和片元着色器 `ResourceId::112`，入口点都是 `main`。
- 当前渲染目标是 `ResourceId::130`，格式为 `R8G8B8A8_UNORM`，viewport 大小为 500x500。
- VS 阶段绑定了一个常量缓冲区 `ubuf`（1216 字节，3 个变量），PS 阶段读取了一个名为 `tex` 的纹理，绑定点为 1。
- 这个抓帧里还能看到一个 256x256 的 `R8G8B8A8_SRGB` 纹理（`ResourceId::97`），以及一个 500x500 的 `D16` 深度附件（`ResourceId::153`）。
- 调试/验证日志为空，当前没有发现报错消息。

如果你愿意，我还可以继续把渲染结果导出成图片，或者进一步查看 shader 反射、反汇编和更多资源详情。
```

这段回答背后，AI 实际会调用一组 MCP 工具，例如：

```text
open_capture({"path":"D:/renderdoc/renderdoc-mcp/tests/fixtures/vkcube.rdc"})
get_capture_info({})
list_draws({"limit":10})
goto_event({"eventId":11})
get_pipeline_state({})
get_bindings({})
list_resources({"type":"Texture"})
get_log({})
```

同一套流程还可以继续导出事件 `11` 的渲染结果，下图就是实际导出的 PNG：

![vkcube 样例抓帧导出的渲染目标](docs/demo/vkcube-render-target.png)

## CLI 工具

`renderdoc-cli` 提供直接的命令行访问方式，适用于脚本、CI 和 Shell 工作流。

运行 `install-codex.ps1` 后，发布包中的 `bin/` 目录会被加入用户级 `PATH`，因此在新的 Shell 中可以直接执行 `renderdoc-cli`。

```bash
# 从运行中的应用程序抓取一帧
renderdoc-cli capture MyApp.exe -d 120 -o capture.rdc

# 分析抓帧文件
renderdoc-cli capture.rdc info
renderdoc-cli capture.rdc events --filter Draw
renderdoc-cli capture.rdc draws
renderdoc-cli capture.rdc pipeline -e 42
renderdoc-cli capture.rdc shader ps -e 42
renderdoc-cli capture.rdc resources --type Texture
renderdoc-cli capture.rdc export-rt 0 -o ./output -e 42
```

### CLI 命令

| 命令 | 说明 |
|------|------|
| `capture EXE [选项]` | 启动应用并注入 RenderDoc，抓取一帧 |
| `info` | 输出抓帧元数据（API、GPU、驱动、总事件数/Draw 数） |
| `events [--filter TEXT]` | 列出所有事件，支持按名称过滤 |
| `draws [--filter TEXT]` | 列出 draw call |
| `pipeline [-e EID]` | 输出指定事件的管线状态 |
| `shader STAGE [-e EID]` | 输出 shader 反汇编（`vs`/`hs`/`ds`/`gs`/`ps`/`cs`） |
| `resources [--type TYPE]` | 按类型过滤并列出资源 |
| `export-rt IDX -o DIR [-e EID]` | 将渲染目标导出到目录 |

## 工具列表（21 个）

### 会话

| 工具 | 说明 |
|------|------|
| `open_capture` | 打开 `.rdc` 文件进行分析，并返回 API 类型和总事件数/Draw 数 |

### 事件与 Draw Call

| 工具 | 说明 |
|------|------|
| `list_events` | 列出所有事件，支持按名称过滤 |
| `goto_event` | 跳转到指定事件 |
| `list_draws` | 列出 draw call，包含顶点/索引数量和实例数 |
| `get_draw_info` | 获取单个 draw call 的详细信息 |

### 管线与绑定

| 工具 | 说明 |
|------|------|
| `get_pipeline_state` | 获取管线状态（shader、RT、viewport），支持可选 `eventId` 参数 |
| `get_bindings` | 获取各个 shader stage 的资源绑定（CBV/SRV/UAV/Sampler） |

### Shader

| 工具 | 说明 |
|------|------|
| `get_shader` | 获取指定 stage 的 shader 反汇编或反射数据（`vs`/`hs`/`ds`/`gs`/`ps`/`cs`） |
| `list_shaders` | 列出所有唯一 shader 及其 stage 和使用次数 |
| `search_shaders` | 在 shader 反汇编文本中搜索模式 |

### 资源

| 工具 | 说明 |
|------|------|
| `list_resources` | 列出所有 GPU 资源，并支持按类型或名称过滤 |
| `get_resource_info` | 获取资源详情（格式、尺寸、字节大小） |
| `list_passes` | 列出渲染 pass（带 draw call 的 marker 区域） |
| `get_pass_info` | 获取 pass 详情，包括其中包含的 draw call |

### 导出

| 工具 | 说明 |
|------|------|
| `export_render_target` | 将当前事件的渲染目标导出为 PNG |
| `export_texture` | 按资源 ID 将任意纹理导出为 PNG |
| `export_buffer` | 将 buffer 数据导出为二进制文件 |

### 信息与诊断

| 工具 | 说明 |
|------|------|
| `get_capture_info` | 获取抓帧元数据：API、GPU、驱动和总事件数/Draw 数 |
| `get_stats` | 获取性能统计：按 pass 分解、热点 draw、最大资源等 |
| `get_log` | 获取调试/验证日志，支持按严重级别和事件过滤 |

### 抓帧

| 工具 | 说明 |
|------|------|
| `capture_frame` | 启动应用并注入 RenderDoc，抓取一帧后自动打开分析 |

## 工具详情

### open_capture

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `path` | string | 是 | `.rdc` 文件的绝对路径 |

**响应：**
```json
{ "api": "D3D12", "totalEvents": 1247, "totalDraws": 312 }
```

---

### list_draws

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|------|------|
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
|------|------|------|------|
| `stage` | string | 是 | shader stage：`vs`、`hs`、`ds`、`gs`、`ps`、`cs` |
| `eventId` | integer | 否 | 事件 ID（省略时使用当前事件） |
| `mode` | string | 否 | `disasm`（默认）或 `reflect` |

**响应（disasm）：**
```json
{ "stage": "ps", "eventId": 42, "disassembly": "ps_5_0\ndcl_globalFlags..." }
```

**响应（reflect）：**
```json
{
  "stage": "ps",
  "inputSignature": [...],
  "constantBlocks": [...],
  "readOnlyResources": [...],
  "readWriteResources": [...]
}
```

---

### get_pipeline_state

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `eventId` | integer | 否 | 要查看的事件 ID（省略时使用当前事件） |

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

> 对 OpenGL 和 Vulkan 抓帧，像素着色器对应的字段名为 `fragmentShader`。

---

### get_bindings

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `eventId` | integer | 否 | 事件 ID（省略时使用当前事件） |

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
|------|------|------|------|
| `type` | string | 否 | 按类型过滤：`Texture`、`Buffer`、`Shader` 等 |
| `name` | string | 否 | 按名称关键字过滤 |

**响应：**
```json
{
  "resources": [
    { "resourceId": "ResourceId::7", "name": "SceneColor", "type": "Texture", "format": "R8G8B8A8_UNORM", "width": 1920, "height": 1080 }
  ],
  "count": 1
}
```

---

### export_texture

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `resourceId` | string | 是 | 资源 ID（如 `ResourceId::123`） |
| `mip` | integer | 否 | Mip 级别（默认 0） |
| `layer` | integer | 否 | 数组层（默认 0） |

---

### export_buffer

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `resourceId` | string | 是 | 资源 ID |
| `offset` | integer | 否 | 字节偏移（默认 0） |
| `size` | integer | 否 | 字节数，0 表示全部（默认 0） |

---

### get_log

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `level` | string | 否 | 最低严重级别：`HIGH`、`MEDIUM`、`LOW`、`INFO` |
| `eventId` | integer | 否 | 按事件 ID 过滤 |

---

### capture_frame

**参数：**

| 名称 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `exePath` | string | 是 | 目标可执行文件的绝对路径 |
| `workingDir` | string | 否 | 工作目录（默认为 exePath 的父目录） |
| `cmdLine` | string | 否 | 目标应用的命令行参数 |
| `delayFrames` | integer | 否 | 抓帧前等待的帧数（默认 100） |
| `outputPath` | string | 否 | `.rdc` 文件路径（默认自动生成到临时目录） |

**响应：**
```json
{
  "api": "D3D12",
  "totalEvents": 1247,
  "totalDraws": 312,
  "path": "C:/Users/.../capture_frame_2025.rdc"
}
```

## 典型工作流

### 分析已有抓帧

```
1. open_capture         -> 打开 .rdc 抓帧文件
2. get_capture_info     -> 查看 API、GPU 和总事件数/Draw 数
3. list_draws           -> 找到感兴趣的 draw call
4. goto_event           -> 跳转到对应 draw call
5. get_pipeline_state   -> 查看 shader、渲染目标和 viewport
6. get_bindings         -> 查看各个 stage 的资源绑定
7. get_shader           -> 读取 shader 反汇编或反射数据
8. export_render_target -> 将渲染目标导出为 PNG
9. get_log              -> 检查验证错误或诊断消息
```

### 抓帧并分析运行中的应用

```
1. capture_frame        -> 启动应用，注入 RenderDoc，抓取一帧并自动打开
2. list_draws           -> 找到感兴趣的 draw call
3. goto_event           -> 跳转到对应 draw call
4. get_pipeline_state   -> 查看 shader、渲染目标和 viewport
5. export_render_target -> 将渲染目标导出为 PNG
```

## 协议细节

| 属性 | 值 |
|------|----|
| 传输方式 | stdio（stdin/stdout） |
| 消息格式 | 按换行分隔的 JSON-RPC 2.0 |
| MCP 协议版本 | 2025-03-26 |
| Batch 支持 | 支持接收；`initialize` 禁止出现在 batch 中 |
| 日志输出 | stderr |

## 架构

```
AI 客户端 (Claude/Codex)              Shell / CI
    |                                     |
    | stdin/stdout (JSON-RPC)             | 命令行
    |                                     |
renderdoc-mcp.exe                    renderdoc-cli.exe
    ├── McpServer (协议层)                |
    ├── ToolRegistry (参数校验)           |
    └── tools/*.cpp (21 个工具)           |
         |                                |
         +---------- core 核心库 ---------+
         |   session, events, pipeline,   |
         |   shaders, resources, export,  |
         |   capture, info, errors        |
         |                                |
         | C++ 动态链接                    |
         |                                |
      renderdoc.dll (Replay API)
```

四层架构：**core**（纯 C++ 库）→ **MCP 服务器**（协议 + 工具）/ **CLI**（命令行）/ **skill**（AI 工作流模式）。单进程、单线程设计，同一时间只支持一个抓帧会话。`ToolRegistry` 会自动执行 `inputSchema` 校验，并返回标准 JSON-RPC `-32602` 错误响应。

## 手动测试

```bash
# 初始化
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}' | renderdoc-mcp.exe

# 列出工具
echo '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | renderdoc-mcp.exe
```

## 许可证

本项目采用 [MIT License](LICENSE)。

RenderDoc 本身采用其自己的许可证，详情请参考 [renderdoc license](https://github.com/baldurk/renderdoc/blob/v1.x/LICENSE.md)。
