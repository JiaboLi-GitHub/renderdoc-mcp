# RenderDoc MCP Analysis Skill

GPU frame capture analysis and debugging using renderdoc-mcp tools.

## Analysis Framework

Every analysis task follows this flow:

```
1. Understand Goal → What does the user want to know?
2. Open & Gather  → Load capture, collect context (parallel)
3. Route          → Pick the right diagnostic workflow
4. Execute        → Drill down with verification at each step
5. Summarize      → Present findings with evidence
```

---

## Phase 1: Open & Gather Context

### Opening a Capture

**From file:** `open_capture` with the .rdc path.
**From app:** `capture_frame` to launch, inject RenderDoc, capture a frame, and auto-open.

**Verification:** Check the returned event count. If 0 events, the capture is empty — report this to the user immediately.

**Error recovery:**
- `open_capture` fails → verify the file path exists and is a valid .rdc file
- `capture_frame` fails → check: exe path correct? app needs admin privileges? app exits immediately? try increasing `delayFrames`

### Initial Context Gathering (parallel)

After a capture is open, call these four tools **in parallel** — they are independent:

| Tool | What it tells you |
|------|-------------------|
| `get_capture_info` | API (D3D11/D3D12/Vulkan/GL), GPU, driver, event count |
| `get_stats` | Per-pass draw/triangle counts, top draws, largest resources |
| `get_log` | Validation errors, debug messages — check HIGH severity first |
| `list_passes` | Frame structure: render pass names and their draw counts |

**Verification:** After gathering, summarize what you learned before proceeding:
- What graphics API is this?
- How many passes and draws?
- Any HIGH-severity validation errors? (If yes, these are likely the root cause — investigate first)
- What are the most expensive passes/draws?

This summary becomes your context for all subsequent analysis.

---

## Phase 2: Route to Workflow

Based on the user's goal and gathered context, pick a workflow:

| User Goal | Workflow |
|-----------|----------|
| "Screen is black / nothing renders" | → Black Screen Diagnosis |
| "Colors are wrong / artifacts" | → Visual Artifact Diagnosis |
| "Performance is bad / too slow" | → Performance Analysis |
| "Explain what this frame does" | → Frame Walkthrough |
| "Debug this specific draw call" | → Targeted Draw Inspection |
| Unclear or general | → Ask the user what they want to investigate |

---

## Diagnostic Workflows

### Black Screen Diagnosis

```
list_draws
 ├─ draws = 0?
 │   → No geometry submitted. Check:
 │     list_events — are there Clear/Dispatch events?
 │     get_log — any errors about pipeline creation or resource binding?
 │     Report: "No draw calls found. Possible causes: ..."
 │
 └─ draws > 0?
     → goto_event (last draw) + get_pipeline_state (parallel)
      ├─ No render target bound?
      │   → Report: "Draw has no render target bound — output goes nowhere"
      │
      └─ Render target IS bound?
          → export_render_target
           ├─ RT has content (not all black)?
           │   → Problem is likely in present/swapchain, not rendering
           │     Check: is the RT the final output? list_events to find Present
           │
           └─ RT is empty/black?
               → get_bindings — check shader resources are bound
               → get_shader ps — check pixel shader output
               → get_shader vs — check vertex shader transforms
               → Report findings with evidence
```

**Parallel opportunity:** `goto_event` + `get_pipeline_state` can run in parallel when both target the same eventId.

### Visual Artifact Diagnosis

```
User identifies problematic draw (or you find it via export_render_target)
 │
 → goto_event (problematic draw)
 → get_pipeline_state + get_bindings (parallel)
 │
 ├─ Check blend state in pipeline — is alpha blending misconfigured?
 ├─ Check render target format — format mismatch (sRGB vs linear)?
 ├─ Check bound textures — missing or wrong textures?
 │   → If suspect texture: export_texture to visually inspect
 │
 └─ Check shaders:
     → get_shader ps mode=disasm — inspect pixel shader logic
     → get_shader ps mode=reflect — check input/output signatures
     → If shader looks wrong: search_shaders with a pattern to find similar
```

**Ask the user:** If multiple draws could be the cause, present the candidates (event IDs + names) and ask which one looks suspect. Export render targets of candidates to help them decide.

### Performance Analysis

```
get_stats (already gathered in Phase 1)
 │
 ├─ Review top draws by triangle count
 │   → goto_event + get_draw_info for the heaviest draws
 │   → get_pipeline_state — check if complex shaders are bound
 │   → get_shader vs/ps mode=reflect — check complexity (input count, CBs)
 │
 ├─ Review largest resources
 │   → get_resource_info for oversized resources
 │   → Are textures larger than needed? Uncompressed when they could be compressed?
 │
 └─ Review per-pass breakdown
     → Which pass has the most draws/triangles?
     → get_pass_info for the heaviest pass
     → Are there redundant draws? (same shader + same resources = likely redundant)
```

**Report format:** Rank issues by impact. For each issue, state: what it is, where it is (event ID), how bad it is (triangle count / resource size), and what could be done about it.

### Frame Walkthrough

```
list_passes — get the high-level structure
 │
 For each pass (or the most interesting ones):
   → get_pass_info — list draws in this pass
   → goto_event (first draw in pass) + get_pipeline_state (parallel)
   → Describe: "Pass '[name]' renders [N] draws with [shader description] to [RT format]"
   → export_render_target — show what this pass produces
 │
 Final: Present a narrative of the frame from start to end
```

**Parallel opportunity:** When walking multiple passes, you can inspect 2-3 passes in parallel if they're independent analysis tasks.

### Targeted Draw Inspection

When the user specifies a draw call (by event ID or name):

```
goto_event + get_pipeline_state + get_bindings (parallel, same eventId)
 │
 → Describe the full pipeline state:
   - Vertex shader: get_shader vs mode=reflect
   - Pixel shader: get_shader ps mode=reflect
   - Bound textures: list from bindings
   - Render targets: from pipeline state
   - Viewport: from pipeline state
 │
 → If user wants deeper inspection:
   - get_shader vs/ps mode=disasm — full disassembly
   - export_render_target — visual output
   - export_texture — inspect bound textures
   - get_draw_info — vertex/index counts
```

---

## Verification Checkpoints

Apply these checks throughout analysis:

| After this step | Verify |
|----------------|--------|
| `open_capture` / `capture_frame` | Event count > 0 |
| `get_log` | Any HIGH severity messages? Investigate first |
| `list_draws` | Draw count matches expectations |
| `get_pipeline_state` | Shaders are bound, RT exists |
| `get_bindings` | Expected resources are bound (not null) |
| `get_shader` returns empty | Stage not bound at this event — try other stages |
| `export_render_target` | Image is not all-black / all-white (if unexpected) |
| Each analysis phase | Summarize: what was found, what was ruled out, what to check next |

---

## Error Recovery

| Error | Recovery |
|-------|----------|
| `open_capture` — file not found | Verify path. Ask user for correct path |
| `open_capture` — invalid file | File may be corrupted or not a .rdc. Ask user to re-capture |
| `capture_frame` — app exits immediately | App may need arguments or specific working directory. Check `cmdLine` and `workingDir` params |
| `capture_frame` — no frame captured | Increase `delayFrames` (app may need more startup time). Check if app renders to a window |
| `get_shader` — empty result | No shader bound at this stage for this event. Try other stages (vs/ps/cs) or other events |
| `get_pipeline_state` — no RT | Some draws don't output to render targets (e.g., stream output). Check draw flags |
| `export_render_target` — index out of range | Check pipeline state for how many RTs are bound. Use index 0-7 |
| `get_resource_info` — invalid ResourceId | List resources first with `list_resources` to get valid IDs |
| Any tool — "no capture open" | Call `open_capture` first |

---

## When to Ask the User

**Ask before proceeding when:**
- Multiple draw calls could be the source of a problem — present candidates with exported RTs
- Analysis is ambiguous — present evidence and your best hypothesis, ask for confirmation
- User's goal is unclear — ask what aspect they want to investigate
- You've found a potential root cause but aren't certain — present it as a hypothesis

**Don't ask, just proceed when:**
- Next step in a diagnostic tree is clear
- Gathering more data to narrow down the problem
- Exporting visuals to support your analysis

---

## Tool Reference

### Session
| Tool | Purpose |
|------|---------|
| `open_capture` | Load .rdc file for analysis |
| `capture_frame` | Launch app → inject → capture → auto-open |

### Navigation & Events
| Tool | Purpose |
|------|---------|
| `list_events` | All events (draws + non-draws), with optional filter |
| `list_draws` | Draw calls only, with optional filter and limit |
| `goto_event` | Navigate to event (updates current event state) |
| `get_draw_info` | Detailed info for a single draw call |

### Pipeline & Bindings
| Tool | Purpose |
|------|---------|
| `get_pipeline_state` | Bound shaders, render targets, depth, viewports |
| `get_bindings` | Shader resource bindings: CBs, textures, UAVs, samplers |

### Shaders
| Tool | Purpose |
|------|---------|
| `get_shader` | Disassembly (`mode=disasm`) or reflection (`mode=reflect`) |
| `list_shaders` | All unique shaders with usage counts |
| `search_shaders` | Text search across all shader disassembly |

### Resources & Passes
| Tool | Purpose |
|------|---------|
| `list_resources` | All GPU resources, filterable by type/name |
| `get_resource_info` | Detailed info for a single resource |
| `list_passes` | Render passes with draw counts |
| `get_pass_info` | Draws within a specific pass |

### Info & Diagnostics
| Tool | Purpose |
|------|---------|
| `get_capture_info` | API, GPU, driver, event count |
| `get_stats` | Performance stats: per-pass breakdown, top draws, largest resources |
| `get_log` | Debug/validation messages, filterable by severity and event |

### Export
| Tool | Purpose |
|------|---------|
| `export_render_target` | Export current event's RT as PNG (index 0-7) |
| `export_texture` | Export texture resource as PNG (by ResourceId) |
| `export_buffer` | Export buffer data as binary file |
