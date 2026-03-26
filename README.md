# renderdoc-mcp

MCP (Model Context Protocol) server for GPU render debugging. Enables AI assistants (Claude, Codex, etc.) to analyze RenderDoc capture files (.rdc) through a standard MCP interface.

## Features

- Open and analyze `.rdc` capture files
- List all draw calls and GPU events with filtering
- Navigate to specific events and inspect pipeline state
- Query bound shaders, render targets, viewports (D3D11 / D3D12 / OpenGL / Vulkan)
- Export render targets as PNG

## Prerequisites

- **Windows** (MSVC compiler with C++17)
- **CMake** >= 3.16
- **RenderDoc** source code and build output (`renderdoc.lib` + `renderdoc.dll`)

## Build

```bash
# Basic (auto-detect renderdoc build output)
cmake -B build -DRENDERDOC_DIR=D:/renderdoc/renderdoc
cmake --build build --config Release

# Explicit build directory
cmake -B build \
  -DRENDERDOC_DIR=D:/renderdoc/renderdoc \
  -DRENDERDOC_BUILD_DIR=D:/renderdoc/renderdoc/build
cmake --build build --config Release
```

### CMake Variables

| Variable | Required | Description |
|----------|----------|-------------|
| `RENDERDOC_DIR` | Yes | Path to renderdoc source root |
| `RENDERDOC_BUILD_DIR` | No | Path to renderdoc build output (if not in standard locations) |

Build output: `build/Release/renderdoc-mcp.exe`

Ensure `renderdoc.dll` is in the same directory as the executable. CMake will copy it automatically if found.

## Client Configuration

### Claude Code

Add to your Claude Code MCP settings (`settings.json`):

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

### Codex / Other MCP Clients

Any MCP client that supports stdio transport can use renderdoc-mcp. Point it to the executable path.

## Tools

### open_capture

Open a RenderDoc capture file for analysis. Closes any previously opened capture.

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `path` | string | Yes | Absolute path to the `.rdc` file |

**Example:**
```json
{
  "name": "open_capture",
  "arguments": {
    "path": "D:/captures/frame_001.rdc"
  }
}
```

**Response:**
```json
{
  "api": "D3D12",
  "eventCount": 1247
}
```

---

### list_events

List all draw calls and actions in the current capture.

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `filter` | string | No | Case-insensitive keyword to filter event names |

**Example:**
```json
{
  "name": "list_events",
  "arguments": {
    "filter": "DrawIndexed"
  }
}
```

**Response:**
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

Navigate to a specific event. Subsequent `get_pipeline_state` and `export_render_target` calls will reflect this event.

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `eventId` | integer | Yes | The event ID to navigate to |

**Example:**
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

Get the graphics pipeline state at the current event. Returns bound shaders, render targets, viewports, and other configuration. Call `goto_event` first.

**Parameters:** None

**Example:**
```json
{
  "name": "get_pipeline_state",
  "arguments": {}
}
```

**Response (D3D12 example):**
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

> For OpenGL and Vulkan captures, the pixel shader key is `fragmentShader`.

---

### export_render_target

Export the current event's render target as a PNG file. The output path is automatically generated.

**Parameters:**

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `index` | integer | No | Render target index (0-7), defaults to 0 |

**Example:**
```json
{
  "name": "export_render_target",
  "arguments": {
    "index": 0
  }
}
```

**Response:**
```json
{
  "path": "D:/captures/renderdoc-mcp-export/rt_42_0.png",
  "width": 1920,
  "height": 1080,
  "eventId": 42,
  "rtIndex": 0
}
```

Output files are saved to `<capture_directory>/renderdoc-mcp-export/`.

## Typical Workflow

```
1. open_capture   → Open a .rdc file
2. list_events    → Browse events, find the draw call of interest
3. goto_event     → Navigate to that event
4. get_pipeline_state → Inspect shaders, render targets, viewports
5. export_render_target → Save the render target as PNG for visual inspection
```

## Protocol Details

| Property | Value |
|----------|-------|
| Transport | stdio (stdin/stdout) |
| Message format | Newline-delimited JSON-RPC 2.0 |
| MCP version | 2025-03-26 |
| Batch support | Yes (receive); `initialize` forbidden in batch |
| Logging | stderr |

## Manual Testing

```bash
# Initialize
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}' | renderdoc-mcp.exe

# List tools
echo '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | renderdoc-mcp.exe
```

## Architecture

```
AI Client (Claude/Codex)
    |
    | stdin/stdout (JSON-RPC, newline-delimited)
    |
renderdoc-mcp.exe
    |
    | C++ dynamic linking
    |
renderdoc.dll (Replay API)
```

Single-process, single-threaded design. One capture session at a time.

## License

See [renderdoc license](https://github.com/baldurk/renderdoc/blob/v1.x/LICENSE.md) for renderdoc components.
