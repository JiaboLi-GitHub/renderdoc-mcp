<p align="center">
  <img src="docs/logo.png" alt="renderdoc-mcp" width="400"><br>
  <b>面向 AI 的 RenderDoc GPU 帧调试工具</b><br><br>
  <a href="https://github.com/JiaboLi-GitHub/renderdoc-mcp/actions/workflows/release.yml"><img src="https://github.com/JiaboLi-GitHub/renderdoc-mcp/actions/workflows/release.yml/badge.svg" alt="Release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License: MIT"></a><br>
  <a href="README.md">English</a> | <b>简体中文</b>
</p>

---

renderdoc-mcp 是一个基于 RenderDoc Replay API 的 MCP Server 和 CLI，提供 **59 个结构化工具**，让 AI 助手（Claude、Codex 等）可以直接打开 `.rdc` 抓帧、分析 GPU 帧、调试 Shader/像素、对比抓帧、导出证据 — 无需手动操作 UI。

## 演示

<video src="https://github.com/JiaboLi-GitHub/renderdoc-mcp/raw/main/docs/demo/demo.mp4" controls width="100%"></video>

## 功能概览

| 模块 | 能力 |
|------|------|
| 会话与抓帧 | 打开抓帧、实时抓帧、查看元信息 |
| 帧导航 | 列出事件/draw call、跳转到任意事件 |
| 管线与 Shader | 查看管线状态、绑定、Shader 源码、常量缓冲区 |
| 资源与 Pass | 分析帧结构、pass 依赖、资源使用情况 |
| 像素与 Shader 调试 | 像素历史、拾取像素、调试像素/顶点/线程 |
| 导出 | 渲染目标、纹理、Buffer、Mesh、快照 |
| Diff 与断言 | 对比两次抓帧，断言像素/状态/图片用于 CI |

## 下载

从 [GitHub Releases](https://github.com/JiaboLi-GitHub/renderdoc-mcp/releases) 获取最新发布包。压缩包内容：

| 文件 | 说明 |
|------|------|
| `bin/renderdoc-mcp.exe` | MCP Server（stdio） |
| `bin/renderdoc-cli.exe` | CLI，用于 Shell 和 CI |
| `bin/renderdoc.dll` / `renderdoc.json` | 内置的 RenderDoc 运行时 |
| `skills/renderdoc-mcp/` | Codex 工作流 skill |
| `install-codex.ps1` | Codex Desktop 一键安装脚本 |

## 客户端配置

### Codex Desktop

安装脚本会自动配置 `~/.codex/config.toml`。也可手动添加：

```toml
[mcp_servers.renderdoc-mcp]
command = 'renderdoc-mcp.exe'
args = []
```

### Claude Code

```json
{
  "mcpServers": {
    "renderdoc-mcp": {
      "command": "renderdoc-mcp.exe",
      "args": []
    }
  }
}
```

## 从源码构建

```bash
cmake -B build -DRENDERDOC_DIR=<path-to-renderdoc-source>
cmake --build build --config Release
```

| 变量 | 必需 | 说明 |
|------|------|------|
| `RENDERDOC_DIR` | 是 | RenderDoc 源码根目录 |
| `RENDERDOC_BUILD_DIR` | 否 | RenderDoc 构建输出目录（非默认位置时使用） |

## 架构

![架构](docs/architecture.png)

## 开源协议

[MIT](LICENSE)。RenderDoc 本身遵循其[上游许可证](https://github.com/baldurk/renderdoc/blob/v1.x/LICENSE.md)。
