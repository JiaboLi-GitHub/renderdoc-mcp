---
name: renderdoc-mcp
description: Analyze RenderDoc GPU frame captures with renderdoc-mcp MCP tools. Use when Codex needs to inspect .rdc captures, diagnose black screens or visual artifacts, explain frame structure, inspect specific draw calls, or investigate GPU rendering and performance issues.
---

# RenderDoc MCP

Use renderdoc-mcp to analyze GPU frame captures and debug rendering problems.

Always use the MCP server named `renderdoc-mcp` for tool calls.

When you need shell-based or batch workflows outside the MCP tool surface, use `renderdoc-cli` from `PATH`.

## Analysis Framework

Every analysis task follows this flow:

```text
1. Understand goal -> what does the user want to know?
2. Open and gather -> load the capture and collect context in parallel
3. Route -> pick the right diagnostic workflow
4. Execute -> drill down with verification at each step
5. Summarize -> present findings with evidence
```

## Phase 1: Open and Gather Context

### Opening a Capture

From file: call `open_capture` with the `.rdc` path.

From app: call `capture_frame` to launch the app, inject RenderDoc, capture a frame, and auto-open it.

Verification: check the returned event count. If it is `0`, the capture is empty and you should report that immediately.

Error recovery:
- If `open_capture` fails, verify the path exists and points to a valid `.rdc` file.
- If `capture_frame` fails, check the executable path, whether the app needs admin privileges, whether it exits immediately, and whether `delayFrames` should be increased.

### Initial Context Gathering

After a capture is open, call these tools in parallel because they are independent:

| Tool | What it tells you |
|------|-------------------|
| `get_capture_info` | API, GPU, driver, event count |
| `get_stats` | Per-pass draw and triangle counts, top draws, largest resources |
| `get_log` | Validation errors and debug messages; check HIGH severity first |
| `list_passes` | Frame structure: pass names and draw counts |

Before moving on, summarize:
- Which graphics API is in use?
- How many passes and draws are present?
- Are there any HIGH-severity validation errors?
- Which passes or draws look most expensive?

Use that summary as the working context for the rest of the analysis.

## Phase 2: Route to a Workflow

Choose a workflow based on the user's goal:

| User goal | Workflow |
|-----------|----------|
| "Screen is black" or "nothing renders" | Black Screen Diagnosis |
| "Colors are wrong" or "there are artifacts" | Visual Artifact Diagnosis |
| "Performance is bad" or "too slow" | Performance Analysis |
| "Explain what this frame does" | Frame Walkthrough |
| "Debug this specific draw call" | Targeted Draw Inspection |
| General or unclear request | Ask the user what they want to investigate |

## Diagnostic Workflows

### Black Screen Diagnosis

```text
list_draws
  draws = 0?
    -> No geometry submitted. Check:
       - list_events for Clear or Dispatch events
       - get_log for pipeline creation or binding errors
       - report "No draw calls found" with likely causes
  draws > 0?
    -> goto_event for the last draw and get_pipeline_state in parallel
       no render target bound?
         -> report that output goes nowhere
       render target bound?
         -> export_render_target
            render target has content?
              -> likely a present or swapchain issue; inspect Present-related events
            render target is black?
              -> inspect bindings and shaders:
                 - get_bindings
                 - get_shader ps
                 - get_shader vs
```

Parallel opportunity: `goto_event` and `get_pipeline_state` can run in parallel when they target the same `eventId`.

### Visual Artifact Diagnosis

```text
Identify the problematic draw, either from the user or by exporting render targets
  -> goto_event for that draw
  -> get_pipeline_state and get_bindings in parallel
     - inspect blend state
     - inspect render target format
     - inspect bound textures
     - export suspicious textures when needed
     - inspect shaders:
       - get_shader ps mode=disasm
       - get_shader ps mode=reflect
       - search_shaders if you need similar shader matches
```

If multiple draws look suspicious, show the candidate event IDs and names, export their render targets, and ask the user which one looks wrong.

### Performance Analysis

```text
Start from get_stats
  -> inspect top draws by triangle count
  -> goto_event and get_draw_info for heavy draws
  -> inspect pipeline complexity with get_pipeline_state
  -> inspect shader reflection with get_shader vs/ps mode=reflect
  -> inspect oversized resources with get_resource_info
  -> inspect the heaviest pass with get_pass_info
  -> look for redundant draws with similar shaders and resources
```

Report issues by impact. For each one, state what it is, where it occurs, how severe it is, and what the likely improvement is.

### Frame Walkthrough

```text
list_passes
  -> for each important pass:
     - get_pass_info
     - goto_event for the first draw and get_pipeline_state in parallel
     - describe the pass inputs, shaders, and outputs
     - export_render_target to show the pass result
  -> end with a narrative from start to finish
```

Parallel opportunity: when passes are independent analysis tasks, inspect two or three in parallel.

### Pixel-Level Diagnosis

When investigating why a pixel has the wrong color or is missing:

1. **pick_pixel** — Read the current pixel color to confirm the issue
2. **pixel_history** — Find which draws modified this pixel, check if any were culled/discarded
3. **debug_pixel** — Trace the fragment shader execution to find where the wrong value comes from
4. **get_texture_stats** — Check if input textures have unexpected ranges (NaN, all-zero, etc.)

### Shader Debugging

When a draw produces wrong output:

1. **debug_vertex** / **debug_pixel** — Trace shader execution with mode="summary" first
2. If inputs look wrong, check bindings with **get_bindings**
3. If logic seems wrong, re-run with mode="trace" for step-by-step execution

### Targeted Draw Inspection

When the user specifies an event ID or draw name:

```text
goto_event + get_pipeline_state + get_bindings in parallel
  -> describe:
     - vertex shader with get_shader vs mode=reflect
     - pixel shader with get_shader ps mode=reflect
     - bound textures from bindings
     - render targets from pipeline state
     - viewport from pipeline state
  -> go deeper when needed:
     - get_shader vs/ps mode=disasm
     - export_render_target
     - export_texture
     - get_draw_info
```

## Verification Checkpoints

Apply these checks throughout the analysis:

| After this step | Verify |
|----------------|--------|
| `open_capture` or `capture_frame` | Event count is greater than 0 |
| `get_log` | HIGH severity messages are investigated first |
| `list_draws` | Draw count matches expectations |
| `get_pipeline_state` | Shaders are bound and a render target exists |
| `get_bindings` | Expected resources are bound and not null |
| `get_shader` returns empty | The stage may not be bound at this event; try a different stage or event |
| `export_render_target` | The image is not unexpectedly all black or all white |
| Each phase | Summarize what was found, ruled out, and what comes next |

## Error Recovery

| Error | Recovery |
|-------|----------|
| `open_capture` file not found | Verify the path and ask for the correct file if needed |
| `open_capture` invalid file | The file may be corrupted or not be an `.rdc`; ask for a new capture |
| `capture_frame` app exits immediately | Check `cmdLine`, `workingDir`, and startup requirements |
| `capture_frame` no frame captured | Increase `delayFrames` and verify the app actually renders to a window |
| `get_shader` empty result | No shader is bound for that stage at this event; try another stage or event |
| `get_pipeline_state` no render target | Some draws do not output to render targets; inspect draw flags |
| `export_render_target` index out of range | Check how many render targets are bound and use a valid index from `0` to `7` |
| `get_resource_info` invalid `ResourceId` | Call `list_resources` first to find a valid ID |
| Any tool says no capture is open | Call `open_capture` first |

## When to Ask the User

Ask before proceeding when:
- Multiple draw calls could be the source of the problem.
- The analysis is ambiguous and you need the user to confirm the most likely hypothesis.
- The user's goal is still unclear after initial context gathering.
- You found a likely root cause but need confirmation before narrowing further.

Do not ask when:
- The next diagnostic step is obvious.
- More data will clearly narrow the problem.
- Exporting an image or texture will give better evidence.

## Tool Reference

### Session

| Tool | Purpose |
|------|---------|
| `open_capture` | Load an `.rdc` file for analysis |
| `capture_frame` | Launch the app, inject RenderDoc, capture a frame, and auto-open it |

### Navigation and Events

| Tool | Purpose |
|------|---------|
| `list_events` | List all events, including draws and non-draws |
| `list_draws` | List draw calls only |
| `goto_event` | Navigate to an event and update current state |
| `get_draw_info` | Retrieve detailed information for one draw call |

### Pipeline and Bindings

| Tool | Purpose |
|------|---------|
| `get_pipeline_state` | Inspect bound shaders, render targets, depth state, and viewports |
| `get_bindings` | Inspect constant buffers, textures, UAVs, and samplers |

### Shaders

| Tool | Purpose |
|------|---------|
| `get_shader` | Retrieve disassembly or reflection |
| `list_shaders` | List unique shaders with usage counts |
| `search_shaders` | Search across shader disassembly text |

### Resources and Passes

| Tool | Purpose |
|------|---------|
| `list_resources` | List GPU resources with optional filtering |
| `get_resource_info` | Inspect a single resource in detail |
| `list_passes` | List render passes with draw counts |
| `get_pass_info` | List draws within one pass |

### Info and Diagnostics

| Tool | Purpose |
|------|---------|
| `get_capture_info` | Inspect API, GPU, driver, and event count |
| `get_stats` | Inspect per-pass breakdowns, top draws, and large resources |
| `get_log` | Inspect debug and validation messages |

### Export

| Tool | Purpose |
|------|---------|
| `export_render_target` | Export the current event's render target as PNG |
| `export_texture` | Export a texture resource as PNG |
| `export_buffer` | Export buffer data as a binary file |

### Pixel & Debug

| Tool | Key Parameters | Purpose |
|------|----------------|---------|
| `pixel_history` | `x`, `y`, `eventId` (opt), `targetIndex` (opt) | Query which draws modified a pixel up to an event; includes shader output, post-blend value, and pass/fail status |
| `pick_pixel` | `x`, `y`, `eventId` (opt), `targetIndex` (opt) | Read the RGBA value of a single pixel; returns float, uint, and int representations |
| `debug_pixel` | `eventId`, `x`, `y`, `mode` (summary/trace), `primitive` (opt) | Debug the fragment shader at a pixel; summary returns inputs/outputs, trace adds step-by-step execution |
| `debug_vertex` | `eventId`, `vertexId`, `mode` (summary/trace), `instance` (opt), `index` (opt), `view` (opt) | Debug the vertex shader for a specific vertex; summary or full trace |
| `debug_thread` | `eventId`, `groupX/Y/Z`, `threadX/Y/Z`, `mode` (summary/trace) | Debug a compute shader thread at a workgroup and thread coordinate |
| `get_texture_stats` | `resourceId`, `mip` (opt), `slice` (opt), `histogram` (opt), `eventId` (opt) | Get min/max pixel values and an optional 256-bucket RGBA histogram; useful for detecting NaN or all-zero textures |
