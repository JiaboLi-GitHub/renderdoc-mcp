# RenderDoc MCP Analysis Skill

Recommended analysis workflow using renderdoc-mcp tools.

## Quick Start

1. `open_capture` — Load a .rdc capture file
2. `get_capture_info` — Understand API, event count, GPU info
3. `list_draws` — Survey draw calls in the frame
4. `goto_event` + `get_pipeline_state` — Inspect a specific draw
5. `get_shader` — Read shader disassembly
6. `export_render_target` — Export visual results

## Common Debugging Scenarios

### Black Screen
1. `list_draws` — Check if draws exist
2. `goto_event` to the last draw
3. `get_pipeline_state` — Check render targets are bound
4. `get_bindings` — Verify shader resources
5. `export_render_target` — Check if RT has content

### Wrong Colors
1. `goto_event` to the problematic draw
2. `get_pipeline_state` — Check blend state
3. `get_shader ps` — Inspect pixel shader
4. `get_bindings` — Check texture bindings

### Performance Analysis
1. `get_stats` — Per-pass breakdown, top draws by triangle count
2. `list_draws` — Find expensive draws
3. `list_resources` — Find large resources

## Tool Reference

| Tool | Purpose |
|------|---------|
| `open_capture` | Load .rdc file |
| `get_capture_info` | Capture metadata |
| `list_events` | All events |
| `list_draws` | Draw calls only |
| `goto_event` | Navigate to event |
| `get_draw_info` | Single draw details |
| `get_pipeline_state` | Pipeline configuration |
| `get_bindings` | Shader resource bindings |
| `list_resources` | All GPU resources |
| `get_resource_info` | Single resource details |
| `list_passes` | Render passes |
| `get_pass_info` | Pass details |
| `get_shader` | Shader disassembly/reflection |
| `list_shaders` | All unique shaders |
| `search_shaders` | Text search in shaders |
| `get_stats` | Performance statistics |
| `get_log` | Debug/validation messages |
| `export_render_target` | Export RT as PNG |
| `export_texture` | Export texture as PNG |
| `export_buffer` | Export buffer as binary |
