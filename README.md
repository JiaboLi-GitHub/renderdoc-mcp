<p align="center">
  <img src="docs/logo.png" alt="renderdoc-mcp" width="400">
</p>

<h1 align="center">renderdoc-mcp</h1>

<p align="center">
  AI-native GPU frame debugging for RenderDoc
</p>

<p align="center">
  <a href="https://github.com/JiaboLi-GitHub/renderdoc-mcp/actions/workflows/release.yml"><img src="https://github.com/JiaboLi-GitHub/renderdoc-mcp/actions/workflows/release.yml/badge.svg" alt="Release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License: MIT"></a>
</p>

<p align="center">
  <b>English</b> | <a href="README-CN.md">简体中文</a>
</p>

---

renderdoc-mcp is an MCP server and CLI that wraps the RenderDoc replay API into **59 structured tools**, letting AI assistants (Claude, Codex, etc.) open `.rdc` captures, inspect GPU frames, debug shaders/pixels, compare captures, and export evidence — all without manual UI.

## Demo

<video src="docs/demo/demo-en.mp4" controls width="100%"></video>

## Features

| Area | What you can do |
|------|----------------|
| Session & Capture | Open captures, live-capture frames, inspect metadata |
| Frame Navigation | List events/draws, jump to any event |
| Pipeline & Shaders | Inspect pipeline state, bindings, shader source, constant buffers |
| Resources & Passes | Analyze frame structure, pass dependencies, resource usage |
| Pixel & Shader Debug | Pixel history, pick pixel, debug pixel/vertex/thread |
| Export | Render targets, textures, buffers, meshes, snapshots |
| Diff & Assertions | Compare captures, assert pixels/state/images for CI |

## Download

Get the latest package from [GitHub Releases](https://github.com/JiaboLi-GitHub/renderdoc-mcp/releases). The zip contains:

| File | Description |
|------|-------------|
| `bin/renderdoc-mcp.exe` | MCP server (stdio) |
| `bin/renderdoc-cli.exe` | CLI for shell & CI |
| `bin/renderdoc.dll` / `renderdoc.json` | Bundled RenderDoc runtime |
| `skills/renderdoc-mcp/` | Codex workflow skill |
| `install-codex.ps1` | One-click Codex Desktop installer |

## Client Configuration

### Codex Desktop

The installer auto-configures `~/.codex/config.toml`. Or add manually:

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

## Build From Source

```bash
cmake -B build -DRENDERDOC_DIR=<path-to-renderdoc-source>
cmake --build build --config Release
```

| Variable | Required | Description |
|----------|----------|-------------|
| `RENDERDOC_DIR` | Yes | RenderDoc source root |
| `RENDERDOC_BUILD_DIR` | No | RenderDoc build output (if non-standard location) |

## Architecture

![Architecture](docs/architecture.png)

## License

[MIT](LICENSE). RenderDoc itself is under its [own license](https://github.com/baldurk/renderdoc/blob/v1.x/LICENSE.md).
