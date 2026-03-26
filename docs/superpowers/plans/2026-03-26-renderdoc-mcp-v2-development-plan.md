# renderdoc-mcp v2+ 开发计划：基于 rdc-cli 命令映射

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 rdc-cli 的 ~70 个 CLI 命令系统性迁移到 renderdoc-mcp MCP server，使 AI 获得完整的 GPU 渲染调试能力。

**Architecture:** 引入 ToolRegistry 模块化架构，每组相关工具独立文件，McpServer 纯协议层 + RenderdocWrapper 纯状态管理 + ToolRegistry 工具注册分发。

**Tech Stack:** C++17, MSVC, CMake 3.16+, nlohmann/json, renderdoc Replay API (C++)

---

## Context

renderdoc-mcp v1 已实现 5 个基础 MCP tools（open_capture, list_events, goto_event, get_pipeline_state, export_render_target）。rdc-cli 已封装 ~70 个 CLI 命令，覆盖完整的 GPU 调试工作流。本计划将 rdc-cli 的能力系统性地迁移到 MCP server，使 AI 获得完整的 GPU 渲染调试能力。

### 关键参考文件

| 用途 | 路径 |
|------|------|
| v1 设计文档 | `docs/superpowers/specs/2026-03-26-renderdoc-mcp-v1-design.md` |
| v1 实现计划 | `docs/superpowers/plans/2026-03-26-renderdoc-mcp-v1-implementation.md` |
| 当前 MCP server | `src/mcp_server.h`, `src/mcp_server.cpp` |
| 当前 wrapper | `src/renderdoc_wrapper.h`, `src/renderdoc_wrapper.cpp` |
| rdc-cli handlers | `D:/renderdoc/rdc-cli/src/rdc/handlers/` |
| rdc-cli services | `D:/renderdoc/rdc-cli/src/rdc/services/` |
| renderdoc 主接口 | `D:/renderdoc/renderdoc/renderdoc/api/replay/renderdoc_replay.h` |
| 管线状态头文件 | `D:/renderdoc/renderdoc/renderdoc/api/replay/d3d11_pipestate.h` 等 |

---

## 架构重构（前置工作）

当前 v1 所有逻辑在 `renderdoc_wrapper.cpp`（545 行，5 个工具）。扩展到 40+ 工具需要模块化重构。

### 目标架构

```
src/
├── main.cpp                          # 入口，stdio 消息循环（不变）
├── mcp_server.h / .cpp               # MCP 协议层，工具注册与分发
├── renderdoc_wrapper.h / .cpp        # 会话管理（open/close/goto），瘦身为状态管理器
├── tool_registry.h / .cpp            # 工具注册表，自动收集 schema + handler
└── tools/
    ├── session_tools.cpp             # open_capture, close_capture, get_status
    ├── event_tools.cpp               # list_events, goto_event, list_draws, get_draw_info
    ├── pipeline_tools.cpp            # get_pipeline_state, get_bindings
    ├── shader_tools.cpp              # get_shader, list_shaders, search_shaders
    ├── resource_tools.cpp            # list_resources, get_resource_info, list_passes
    ├── export_tools.cpp              # export_texture, export_buffer, export_mesh, export_render_target
    ├── pixel_tools.cpp               # get_pixel_history, pick_pixel
    ├── debug_tools.cpp               # debug_pixel, debug_vertex, debug_thread
    ├── shader_edit_tools.cpp         # build_shader, replace_shader, restore_shader
    ├── info_tools.cpp                # get_capture_info, get_stats, get_log, get_counters
    └── assert_tools.cpp              # assert_pixel, assert_clean, assert_count, assert_state
```

### 工具注册机制

```cpp
// tool_registry.h
struct ToolDef {
    std::string name;
    std::string description;
    nlohmann::json inputSchema;
    // handler 返回裸 JSON 业务数据（如 {api: "D3D12", eventCount: 1247}），
    // 抛 std::runtime_error 表示工具级错误。
    std::function<nlohmann::json(RenderdocWrapper&, const nlohmann::json&)> handler;
};

// 协议级参数错误（missing required / wrong type），
// 由 McpServer 捕获并转为 JSON-RPC -32602 error response
struct InvalidParamsError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class ToolRegistry {
public:
    void registerTool(ToolDef def);
    nlohmann::json getToolDefinitions() const;           // for tools/list
    // callTool 内部流程：
    //   1. 查找工具（未找到 → 抛 InvalidParamsError）
    //   2. 校验 args 与 inputSchema（失败 → 抛 InvalidParamsError）
    //   3. 调用 handler（handler 抛 std::runtime_error → 工具级错误）
    nlohmann::json callTool(const std::string& name,     // for tools/call
                            RenderdocWrapper& wrapper,
                            const nlohmann::json& args);
    bool hasTool(const std::string& name) const;
};
```

**inputSchema 校验策略**：`callTool()` 在调用 handler 之前，对 `args` 做轻量级 JSON Schema 校验：
1. 检查 `inputSchema.required` 中的字段是否都存在于 `args` 中
2. 对已存在的字段检查 `type` 是否匹配（string/integer/number/boolean/object/array）
3. 校验失败时抛 `InvalidParamsError`，消息格式如 `"Missing required parameter: eventId"` 或 `"Parameter 'eventId' must be integer"`

不做完整 JSON Schema 校验（无需 $ref、oneOf、pattern 等），覆盖三项：
1. **required** — 必填字段缺失检查
2. **type** — 字段类型匹配（string/integer/number/boolean/object/array）
3. **enum** — 枚举值范围检查（如 `stage: "vs"|"ps"|...`、`mode: "disasm"|"source"|...`）
4. **根 object** — `args` 本身必须是 JSON object

这四项足以把参数错误（包括 `stage="foo"` 之类的非法枚举值）归为协议错误 `-32602`。

**MCP CallToolResult 包装职责在 McpServer 一侧**：`McpServer::handleToolsCall()` 调用 `registry.callTool()` 获取裸 JSON 后，由 `McpServer::makeToolResult(data)` 统一包装为 MCP 规范的 `{content: [{type: "text", text: ...}], isError: false}` 格式。handler 抛异常时，`handleToolsCall()` 的 catch 块调用 `makeToolResult(e.what(), true)` 生成错误响应。这保持了 v1 的包装链路（参见 `mcp_server.cpp:222-266`），工具函数无需关心 MCP 协议细节。

每个 `tools/*.cpp` 文件提供 `registerXxxTools(ToolRegistry&)` 函数注册自己的工具。McpServer 持有 ToolRegistry 实例，tools/list 和 tools/call 委托给它。

---

## rdc-cli 命令 → MCP Tool 完整映射

### Phase 2：核心分析增强（最高优先级）

为 AI 提供完整的帧内容理解能力。

| rdc-cli 命令 | MCP Tool | 说明 |
|---|---|---|
| `info` | `get_capture_info` | 捕获元数据（分辨率、API、时长、GPU） |
| `draws` | `list_draws` | draw call 列表含统计（顶点数、实例数） |
| `draw` | `get_draw_info` | 单个 draw call 详情 |
| `shader` | `get_shader` | shader 反汇编/源码/反射/常量 |
| `shaders` | `list_shaders` | 全部 shader 列表 |
| `search` | `search_shaders` | shader 文本正则搜索 |
| `resources` | `list_resources` | 全部 GPU 资源列表 |
| `resource` | `get_resource_info` | 资源详情 |
| `pipeline` | **增强** `get_pipeline_state` | 扩展到完整管线段（topology, blend, depth-stencil, rasterizer 等） |
| `bindings` | `get_bindings` | 描述符绑定表 |
| `passes` | `list_passes` | 渲染 pass 列表 |
| `pass` | `get_pass_info` | pass 详情含 draw 列表 |
| `texture` | `export_texture` | 导出任意纹理（by resource ID） |
| `buffer` | `export_buffer` | 导出 buffer 数据 |
| `rt` | **增强** `export_render_target` | 增加 depth target 支持 |
| `stats` | `get_stats` | 性能统计（per-pass、top draws、largest resources） |
| `log` | `get_log` | 调试/验证消息 |

### Phase 3：调试能力（高优先级）

AI 定位渲染问题的核心手段。

| rdc-cli 命令 | MCP Tool | 说明 |
|---|---|---|
| `pixel` | `get_pixel_history` | 像素修改历史 |
| `pick-pixel` | `pick_pixel` | 查询像素当前颜色 |
| `debug pixel` | `debug_pixel` | 像素着色器逐步调试 |
| `debug vertex` | `debug_vertex` | 顶点着色器逐步调试 |
| `debug thread` | `debug_thread` | 计算着色器线程调试 |
| `callstacks` | `get_callstacks` | CPU 调用栈 |
| `usage` | `get_resource_usage` | 事件的资源使用情况 |
| `counters` | `get_counters` | GPU 性能计数器 |
| `tex-stats` | `get_texture_stats` | 纹理内存统计 |

### Phase 4：Shader 编辑（中优先级）

支持 AI 修改 shader 并验证效果。

| rdc-cli 命令 | MCP Tool | 说明 |
|---|---|---|
| `shader-encodings` | `list_shader_encodings` | 可用编码格式 |
| `shader-build` | `build_shader` | 编译 shader 源码 |
| `shader-replace` | `replace_shader` | 替换 draw call 的 shader |
| `shader-restore` | `restore_shader` | 恢复原始 shader |
| `shader-restore-all` | `restore_all_shaders` | 恢复全部 shader |
| `mesh` | `export_mesh` | 导出网格为 OBJ |
| `snapshot` | `take_snapshot` | 综合调试快照 |

### Phase 5：CI/验证（中优先级）

支持 AI 驱动的自动化渲染验证。

| rdc-cli 命令 | MCP Tool | 说明 |
|---|---|---|
| `assert-pixel` | `assert_pixel` | 像素颜色断言 |
| `assert-clean` | `assert_clean` | 验证消息断言 |
| `assert-count` | `assert_count` | 事件计数断言 |
| `assert-state` | `assert_state` | 管线状态断言 |
| `assert-image` | `assert_image` | 图像对比断言 |
| `diff` | `diff_captures` | 两个 capture 对比（需双会话） |

### Phase 6：VFS 浏览（低优先级）

对 AI 来说，结构化工具调用比文件系统浏览更自然，可作为辅助。

| rdc-cli 命令 | MCP Tool | 说明 |
|---|---|---|
| `ls` | `vfs_ls` | 虚拟文件系统目录列表 |
| `cat` | `vfs_cat` | 虚拟文件系统文件内容 |
| `tree` | `vfs_tree` | 虚拟文件系统树形结构 |

### 不迁移的命令

以下 rdc-cli 命令不适合 MCP server 场景，不计划迁移：

| 命令 | 原因 |
|---|---|
| `capture`, `attach` | 需要启动/注入进程，MCP server 专注于分析已有 capture |
| `remote connect/list/capture` | 需要网络连接远程设备 |
| `android setup/capture/stop/list` | 需要 adb 和 Android 设备 |
| `setup-renderdoc`, `doctor` | 环境搭建工具 |
| `serve` | HTTP Web UI，与 MCP 架构冲突 |
| `script` | 任意 Python 脚本执行，安全风险 |
| `completion`, `install-skill` | CLI 专用 |
| `sections`, `section` | 底层 capture 文件操作，AI 通常不需要 |
| `count`, `shader-map`, `unused-targets` | 可通过组合现有工具实现 |
| `gpus` | 合并到 get_capture_info |

---

## Phase 2 详细实现计划

Phase 2 是最高优先级，提供 AI 完整理解帧内容的能力。

### Task 0：架构重构 — ToolRegistry

**Files:**
- Create: `src/tool_registry.h`, `src/tool_registry.cpp`
- Create: `src/tools/session_tools.cpp`（open_capture, close_capture）
- Create: `src/tools/event_tools.cpp`（list_events, goto_event）
- Create: `src/tools/pipeline_tools.cpp`（get_pipeline_state）
- Create: `src/tools/export_tools.cpp`（export_render_target）
- Modify: `src/mcp_server.h`, `src/mcp_server.cpp`
- Modify: `src/renderdoc_wrapper.h`, `src/renderdoc_wrapper.cpp`
- Modify: `CMakeLists.txt`
- Delete: `src/tools/open_capture.cpp`, `src/tools/list_events.cpp`, `src/tools/goto_event.cpp`, `src/tools/get_pipeline_state.cpp`, `src/tools/export_render_target.cpp`（占位文件）

**目标**: 引入 ToolRegistry，将全部 5 个现有工具一次性迁移到新机制，McpServer 瘦身为纯协议层。

- [ ] **Step 1: 创建 ToolRegistry**

```cpp
// src/tool_registry.h
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <stdexcept>
#include <nlohmann/json.hpp>

class RenderdocWrapper;

// 协议级参数错误 → McpServer 转为 JSON-RPC -32602
struct InvalidParamsError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ToolDef {
    std::string name;
    std::string description;
    nlohmann::json inputSchema;
    // 返回裸 JSON 业务数据，抛 std::runtime_error 表示工具级错误
    std::function<nlohmann::json(RenderdocWrapper&, const nlohmann::json&)> handler;
};

class ToolRegistry {
public:
    void registerTool(ToolDef def);
    nlohmann::json getToolDefinitions() const;
    // 内部流程: 查找 → 校验 inputSchema → 调用 handler
    // 校验失败抛 InvalidParamsError，handler 失败抛 std::runtime_error
    nlohmann::json callTool(const std::string& name,
                            RenderdocWrapper& wrapper,
                            const nlohmann::json& args);
    bool hasTool(const std::string& name) const;

private:
    // 轻量级 JSON Schema 校验：required 字段存在性 + type 匹配
    void validateArgs(const ToolDef& tool, const nlohmann::json& args) const;

    std::vector<ToolDef> m_tools;
    std::unordered_map<std::string, size_t> m_toolIndex;
};
```

- [ ] **Step 2: 实现 ToolRegistry**

```cpp
// src/tool_registry.cpp
void ToolRegistry::registerTool(ToolDef def) {
    m_toolIndex[def.name] = m_tools.size();
    m_tools.push_back(std::move(def));
}

nlohmann::json ToolRegistry::getToolDefinitions() const {
    auto tools = nlohmann::json::array();
    for (const auto& t : m_tools) {
        tools.push_back({
            {"name", t.name},
            {"description", t.description},
            {"inputSchema", t.inputSchema}
        });
    }
    return tools;
}

nlohmann::json ToolRegistry::callTool(const std::string& name,
                                       RenderdocWrapper& wrapper,
                                       const nlohmann::json& args) {
    auto it = m_toolIndex.find(name);
    if (it == m_toolIndex.end()) {
        throw InvalidParamsError("Unknown tool: " + name);
    }
    const auto& tool = m_tools[it->second];
    validateArgs(tool, args);  // 抛 InvalidParamsError
    return tool.handler(wrapper, args);  // 抛 std::runtime_error → 工具错误
}

void ToolRegistry::validateArgs(const ToolDef& tool, const nlohmann::json& args) const {
    const auto& schema = tool.inputSchema;
    if (!schema.contains("properties")) return;

    // 1. required 字段检查
    if (schema.contains("required") && schema["required"].is_array()) {
        for (const auto& req : schema["required"]) {
            const auto& fieldName = req.get<std::string>();
            if (!args.contains(fieldName)) {
                throw InvalidParamsError("Missing required parameter: " + fieldName);
            }
        }
    }

    // 2. 校验 args 本身必须是 object（schema.type == "object"）
    if (!args.is_object()) {
        throw InvalidParamsError("Arguments must be an object");
    }

    // 3. type + enum 检查（仅检查 args 中实际存在的字段）
    const auto& props = schema["properties"];
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (!props.contains(it.key())) continue;
        const auto& propSchema = props[it.key()];
        const auto& val = it.value();

        // type 检查
        if (propSchema.contains("type")) {
            const auto& expectedType = propSchema["type"].get<std::string>();
            bool ok = false;
            if (expectedType == "string")       ok = val.is_string();
            else if (expectedType == "integer") ok = val.is_number_integer();
            else if (expectedType == "number")  ok = val.is_number();
            else if (expectedType == "boolean") ok = val.is_boolean();
            else if (expectedType == "object")  ok = val.is_object();
            else if (expectedType == "array")   ok = val.is_array();
            else ok = true;  // 未知类型不校验
            if (!ok) {
                throw InvalidParamsError(
                    "Parameter '" + it.key() + "' must be " + expectedType);
            }
        }

        // enum 检查（适用于 stage、mode 等枚举字段）
        if (propSchema.contains("enum") && propSchema["enum"].is_array()) {
            bool found = false;
            for (const auto& allowed : propSchema["enum"]) {
                if (val == allowed) { found = true; break; }
            }
            if (!found) {
                std::string allowedStr;
                for (const auto& a : propSchema["enum"]) {
                    if (!allowedStr.empty()) allowedStr += ", ";
                    allowedStr += a.dump();
                }
                throw InvalidParamsError(
                    "Parameter '" + it.key() + "' must be one of: " + allowedStr);
            }
        }
    }
}
```

- [ ] **Step 3: RenderdocWrapper 暴露访问器**

在 `renderdoc_wrapper.h` 增加公开方法，使工具函数可直接访问底层状态：

```cpp
// 供工具函数使用的访问器
IReplayController* getController() const { return m_controller; }
ICaptureFile* getCaptureFile() const { return m_captureFile; }
uint32_t getCurrentEventId() const { return m_currentEventId; }
const std::string& getCapturePath() const { return m_capturePath; }
std::string getExportDir() const;  // 改为 public
```

- [ ] **Step 4: 迁移全部 5 个工具到 4 个 tools/*.cpp 文件**

将现有 5 个工具的逻辑从 `renderdoc_wrapper.cpp` 提取为独立函数，分别放入：
- `src/tools/session_tools.cpp` → `registerSessionTools(ToolRegistry&)` → open_capture
- `src/tools/event_tools.cpp` → `registerEventTools(ToolRegistry&)` → list_events, goto_event
- `src/tools/pipeline_tools.cpp` → `registerPipelineTools(ToolRegistry&)` → get_pipeline_state
- `src/tools/export_tools.cpp` → `registerExportTools(ToolRegistry&)` → export_render_target

**注意**：4 个文件必须同步创建，因为 Step 5 会注册全部 4 组。此 Step 的交付物是 4 个 `.cpp` 文件 + 对应的注册函数声明头文件（可统一放在 `src/tools/tools.h`）。

- [ ] **Step 5: 修改 McpServer 使用 ToolRegistry**

```cpp
// mcp_server.h 变更：
// - 删除 getToolDefinitions() 和 callTool() 方法
// - 增加成员 ToolRegistry m_registry
// - 构造函数中注册全部 4 组工具

McpServer::McpServer() {
    registerSessionTools(m_registry);    // open_capture
    registerEventTools(m_registry);      // list_events, goto_event
    registerPipelineTools(m_registry);   // get_pipeline_state
    registerExportTools(m_registry);     // export_render_target
}

// handleToolsList: return m_registry.getToolDefinitions()
// handleToolsCall 三层错误处理:
//
//   if (toolName.empty())
//       return makeError(id, -32602, "Invalid params: missing tool name");
//   try {
//       json rawResult = m_registry.callTool(toolName, m_wrapper, arguments);
//       return makeResponse(id, makeToolResult(rawResult));
//   } catch (const InvalidParamsError& e) {
//       // 协议级错误：unknown tool / missing required / type mismatch
//       return makeError(id, -32602, std::string("Invalid params: ") + e.what());
//   } catch (const std::exception& e) {
//       // 工具级错误：renderdoc API 失败、无 capture 打开等
//       return makeResponse(id, makeToolResult(e.what(), true));
//   }
//
// InvalidParamsError 由 ToolRegistry::callTool 内部的 validateArgs 抛出，
// 覆盖 unknown tool、missing required params、type mismatch 三类。
// handler 内部的 std::runtime_error 保持为工具级错误。
```

- [ ] **Step 6: 更新 CMakeLists.txt**

- 添加新源文件：`src/tool_registry.cpp`, `src/tools/session_tools.cpp`, `src/tools/event_tools.cpp`, `src/tools/pipeline_tools.cpp`, `src/tools/export_tools.cpp`
- 删除旧占位文件：`src/tools/open_capture.cpp` 等 5 个

- [ ] **Step 7: 编译验证**

```bash
cmake -B build -DRENDERDOC_DIR=D:/renderdoc/renderdoc
cmake --build build --config Release
```

- [ ] **Step 8: 端到端回归测试**

验证 5 个原有工具功能不变（open_capture → list_events → goto_event → get_pipeline_state → export_render_target 全链路）。

---

### Task 1：get_capture_info

**Files:**
- Create: `src/tools/info_tools.cpp`
- Modify: `src/mcp_server.cpp`（注册）

**对应 rdc-cli**: `info`, `gpus`

- [ ] **Step 1: 实现 get_capture_info**

```cpp
// src/tools/info_tools.cpp
void registerInfoTools(ToolRegistry& registry) {
    registry.registerTool({
        "get_capture_info",
        "Get metadata about the currently opened capture file including API, GPU, resolution, and event count",
        {{"type", "object"}, {"properties", json::object()}},
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if (!ctrl) throw std::runtime_error("No capture open");

            auto props = ctrl->GetAPIProperties();
            // 提取 API 类型、版本信息
            // 统计事件数
            // 获取 capture 文件路径信息
            return { /* ... */ };
        }
    });
}
```

**renderdoc API 调用**:
- `controller->GetAPIProperties()` → pipelineType, degraded（`IReplayController`，`renderdoc_replay.h:431`）
- `captureFile->RecordedMachineIdent()` → 捕获机器标识（`ICaptureFile`，`renderdoc_replay.h:1676`）
- `captureFile->GetAvailableGPUs()` → GPU 列表，每个 `GPUDevice` 含 name/vendor/deviceID/driver（`ICaptureAccess`，`renderdoc_replay.h:1252`；rdc-cli 的 `_handle_capture_gpus` 用此接口获取 GPU 信息）
- `captureFile->HasCallstacks()` → 是否含 CPU 调用栈
- `captureFile->TimestampBase()` → 时间戳基准
- `controller->GetRootActions()` → 递归计数事件数
- 注意：`ICaptureAccess::DriverName()`（`renderdoc_replay.h:1348`）返回驱动名称字符串，owning interface 是 `ICaptureAccess`，`ICaptureFile` 通过继承可访问

- [ ] **Step 2: 注册到 McpServer**
- [ ] **Step 3: 编译验证**
- [ ] **Step 4: 测试**
- [ ] **Step 5: 提交**

---

### Task 2：list_draws / get_draw_info

**Files:**
- Modify: `src/tools/event_tools.cpp`

**对应 rdc-cli**: `draws`, `draw`

- [ ] **Step 1: 实现 list_draws**

过滤 ActionFlags::Drawcall 的事件，返回含顶点/实例计数的列表。

```json
{
  "name": "list_draws",
  "description": "List draw calls with vertex/index counts, instance counts, and flags",
  "inputSchema": {
    "type": "object",
    "properties": {
      "filter": { "type": "string", "description": "Filter by name keyword" },
      "pass": { "type": "string", "description": "Filter by pass name" },
      "limit": { "type": "integer", "description": "Max results", "default": 1000 }
    }
  }
}
```

**返回字段**: eventId, name, flags, numIndices, numInstances, drawIndex, outputs

**renderdoc API**:
- `controller->GetRootActions()` → 递归遍历
- `action.flags & ActionFlags::Drawcall` → 过滤
- `action.numIndices`, `action.numInstances`, `action.drawIndex`

- [ ] **Step 2: 实现 get_draw_info**

跳转到指定事件并返回详细 draw call 信息（含 output targets）。

- [ ] **Step 3: 编译验证 + 测试**
- [ ] **Step 4: 提交**

---

### Task 3：get_shader / list_shaders / search_shaders

**Files:**
- Create: `src/tools/shader_tools.cpp`

**对应 rdc-cli**: `shader`, `shaders`, `search`

这是 Phase 2 最复杂的工具组。

- [ ] **Step 1: 实现 get_shader（disasm 模式）**

```json
{
  "name": "get_shader",
  "description": "Get shader disassembly, source code, reflection data, or constant buffer values",
  "inputSchema": {
    "type": "object",
    "properties": {
      "eventId": { "type": "integer", "description": "Event ID (uses current if omitted)" },
      "stage": { "type": "string", "enum": ["vs","hs","ds","gs","ps","cs"] },
      "mode": { "type": "string", "enum": ["disasm","source","reflect","constants"], "default": "disasm" }
    },
    "required": ["stage"]
  }
}
```

**renderdoc API（disasm 模式）**:
```cpp
auto* pipe = getPipelineState(ctrl, api);  // 根据 API 类型获取 PipeState
ResourceId shaderId = pipe->GetShader(stageEnum);
ShaderReflection* refl = pipe->GetShaderReflection(stageEnum);
rdcarray<rdcstr> targets = ctrl->GetDisassemblyTargets(true);
// 注意：CS 用 GetComputePipelineObject()，其他 stage 用 GetGraphicsPipelineObject()
// 参见 renderdoc/api/replay/pipestate.h:264-271
ResourceId pipelineId = (stageEnum == ShaderStage::Compute)
    ? pipe->GetComputePipelineObject()
    : pipe->GetGraphicsPipelineObject();
rdcstr disasm = ctrl->DisassembleShader(pipelineId, refl, targets[0]);
```

- [ ] **Step 2: 实现 get_shader（source 模式）**

通过 `refl->debugInfo.files` 获取原始源码。

- [ ] **Step 3: 实现 get_shader（reflect 模式）**

提取 `refl->inputSignature`, `outputSignature`, `constantBlocks`, `readOnlyResources`, `readWriteResources`。

- [ ] **Step 4: 实现 get_shader（constants 模式）**

```cpp
// 与 disasm 模式相同，需按 stage 选择正确的 pipeline object
ResourceId pipelineId = (stageEnum == ShaderStage::Compute)
    ? pipe->GetComputePipelineObject()
    : pipe->GetGraphicsPipelineObject();
for (int i = 0; i < refl->constantBlocks.count(); i++) {
    auto desc = pipe->GetConstantBlock(stageEnum, i, 0);
    auto vars = ctrl->GetCBufferVariableContents(
        pipelineId, shaderId, stageEnum,
        pipe->GetShaderEntryPoint(stageEnum), i,
        desc.descriptor.resource, desc.descriptor.byteOffset,
        desc.descriptor.byteSize);
    // 序列化变量值
}
```

- [ ] **Step 5: 实现 list_shaders**

遍历所有 draw call，收集唯一 shader ID 及其使用次数。

- [ ] **Step 6: 实现 search_shaders**

对所有 shader 执行 DisassembleShader，用正则搜索匹配行。

- [ ] **Step 7: 编译验证 + 测试**
- [ ] **Step 8: 提交**

---

### Task 4：list_resources / get_resource_info

**Files:**
- Create: `src/tools/resource_tools.cpp`

**对应 rdc-cli**: `resources`, `resource`

- [ ] **Step 1: 实现 list_resources**

```json
{
  "name": "list_resources",
  "description": "List all GPU resources (textures, buffers, render targets) in the capture",
  "inputSchema": {
    "type": "object",
    "properties": {
      "type": { "type": "string", "description": "Filter by type: Texture, Buffer, Shader, etc." },
      "name": { "type": "string", "description": "Filter by name keyword" }
    }
  }
}
```

**renderdoc API**:
```cpp
rdcarray<ResourceDescription> resources = ctrl->GetResources();
// 每个: resourceId, name, type, format, width, height, depth, byteSize, creationFlags
```

- [ ] **Step 2: 实现 get_resource_info**

按 resource ID 获取详情，包括格式、尺寸、mip 级别等。

- [ ] **Step 3: 编译验证 + 测试**
- [ ] **Step 4: 提交**

---

### Task 5：增强 get_pipeline_state

**Files:**
- Modify: `src/tools/pipeline_tools.cpp`

**对应 rdc-cli**: `pipeline` 完整段支持

- [ ] **Step 1: 增加 section 参数**

```json
"section": {
  "type": "string",
  "enum": ["all","topology","viewport","scissor","blend","stencil",
           "rasterizer","depth-stencil","msaa","vbuffers","ibuffer",
           "samplers","vinputs","vs","hs","ds","gs","ps","cs"],
  "default": "all"
}
```

- [ ] **Step 2: 实现各段提取**

每个段对应管线状态的一部分，例如：
- `topology`: `inputAssembly.topology`
- `blend`: `outputMerger.blendState`
- `vbuffers`: `inputAssembly.vertexBuffers`
- `ibuffer`: `inputAssembly.indexBuffer`

- [ ] **Step 3: 增加 eventId 参数**

允许直接指定事件而非依赖 goto_event。

- [ ] **Step 4: 编译验证 + 测试**
- [ ] **Step 5: 提交**

---

### Task 6：get_bindings

**Files:**
- Modify: `src/tools/pipeline_tools.cpp`

**对应 rdc-cli**: `bindings`

- [ ] **Step 1: 实现 get_bindings**

```json
{
  "name": "get_bindings",
  "description": "Get descriptor/resource bindings for all shader stages at an event",
  "inputSchema": {
    "type": "object",
    "properties": {
      "eventId": { "type": "integer", "description": "Event ID (uses current if omitted)" }
    }
  }
}
```

**renderdoc API**: 遍历各 stage 的 reflection 数据提取 CBV/SRV/UAV/Sampler 绑定。

- [ ] **Step 2: 编译验证 + 测试**
- [ ] **Step 3: 提交**

---

### Task 7：list_passes / get_pass_info

**Files:**
- Modify: `src/tools/resource_tools.cpp`

**对应 rdc-cli**: `passes`, `pass`

- [ ] **Step 1: 实现 list_passes**

分析 action 树层级结构，提取 marker regions 作为 pass。

- [ ] **Step 2: 实现 get_pass_info**

返回指定 pass 内的 draw call 列表及统计。

- [ ] **Step 3: 编译验证 + 测试**
- [ ] **Step 4: 提交**

---

### Task 8：export_texture / export_buffer

**Files:**
- Modify: `src/tools/export_tools.cpp`

**对应 rdc-cli**: `texture`, `buffer`

- [ ] **Step 1: 实现 export_texture**

```json
{
  "name": "export_texture",
  "description": "Export a texture resource as PNG image",
  "inputSchema": {
    "type": "object",
    "properties": {
      "resourceId": { "type": "string", "description": "Resource ID to export" },
      "mip": { "type": "integer", "default": 0 },
      "layer": { "type": "integer", "default": 0 }
    },
    "required": ["resourceId"]
  }
}
```

**renderdoc API**:
```cpp
TextureSave save = {};
save.resourceId = parseResourceId(resourceId);
save.mip = mip;
save.slice.sliceIndex = layer;
save.destType = FileType::PNG;
ctrl->SaveTexture(save, outputPath);
```

- [ ] **Step 2: 实现 export_buffer**

```cpp
rdcarray<byte> data = ctrl->GetBufferData(resourceId, offset, size);
// 写入二进制文件
```

- [ ] **Step 3: 增强 export_render_target 支持 depth target**

- [ ] **Step 4: 编译验证 + 测试**
- [ ] **Step 5: 提交**

---

### Task 9：get_stats / get_log

**Files:**
- Modify: `src/tools/info_tools.cpp`

**对应 rdc-cli**: `stats`, `log`

- [ ] **Step 1: 实现 get_stats**

遍历 action 树，统计 per-pass draw 数、总顶点数、资源大小 top N。

- [ ] **Step 2: 实现 get_log**

```json
{
  "name": "get_log",
  "description": "Get debug/validation messages from the capture",
  "inputSchema": {
    "type": "object",
    "properties": {
      "level": { "type": "string", "enum": ["HIGH","MEDIUM","LOW","INFO"], "description": "Minimum severity" },
      "eventId": { "type": "integer", "description": "Filter by event ID" }
    }
  }
}
```

**renderdoc API**:
```cpp
rdcarray<DebugMessage> msgs = ctrl->GetDebugMessages();
// 过滤 severity 和 eventId
```

- [ ] **Step 3: 编译验证 + 测试**
- [ ] **Step 4: 提交**

---

## Phase 3-6 概要

Phase 3-6 各自作为独立子项目，在 Phase 2 完成后分别制定详细实现计划。

### Phase 3 关键技术点
- `controller->PixelHistory(resourceId, x, y, subresource, compType)` → 像素历史
- `controller->DebugPixel/DebugVertex/DebugThread()` + `ContinueDebug()` → shader 调试 trace
- 调试 trace 可能产生大量数据，需要分页或摘要机制
- `controller->FreeTrace(trace)` → 释放调试资源

### Phase 4 关键技术点
- `controller->BuildCustomShader()` 或 `BuildTargetShader()` → 编译 shader
- `controller->ReplaceResource()` → 替换 shader
- `controller->RemoveReplacement()` → 恢复
- 需要管理 built shader 的生命周期（ID 映射表）
- `controller->GetPostVSData()` → mesh 数据导出

### Phase 5 关键技术点
- diff 需要同时打开两个 capture → 需要扩展会话模型支持多 IReplayController
- assert 工具返回结构化 pass/fail 结果 + 退出码语义
- assert_image 需要像素级图像对比算法

### Phase 6 关键技术点
- VFS 是 rdc-cli 的独特设计，将 GPU 状态映射为文件系统路径
- 对 MCP server 而言，结构化工具已覆盖大部分场景，VFS 作为补充浏览手段
- 路由表参考 `D:/renderdoc/rdc-cli/src/rdc/vfs/router.py`

---

## 工具总数统计

| Phase | 新增工具数 | 累计工具数 | 状态 |
|---|---|---|---|
| v1（已完成） | 5 | 5 | ✅ Done |
| Phase 2：核心分析 | ~17 | ~22 | 待实施 |
| Phase 3：调试能力 | ~9 | ~31 | 待规划 |
| Phase 4：Shader 编辑 | ~7 | ~38 | 待规划 |
| Phase 5：CI/验证 | ~6 | ~44 | 待规划 |
| Phase 6：VFS 浏览 | ~3 | ~47 | 待规划 |

---

## 验证策略

每个 Task 的验证：
1. **编译验证**: `cmake --build build --config Release` 通过
2. **协议验证**: echo JSON-RPC 消息到 stdin，验证响应格式正确
3. **功能验证**: 用实际 .rdc 文件测试每个新工具
4. **回归验证**: 确保已有工具行为不变
5. **Claude Code 集成验证**: 配置为 MCP server，AI 实际调用验证

---

## 执行建议

1. **先执行 Task 0（架构重构）** — 这是所有后续工作的基础
2. **Phase 2 按 Task 1-9 顺序执行** — 每个 Task 独立可测试
3. **每个 Task 完成后立即提交** — 保持小步增量
4. **Phase 2 全部完成后，再制定 Phase 3 详细计划** — 避免过度前瞻设计
5. **建议使用 subagent-driven-development** — 每个 Task 一个 subagent，主会话 review
