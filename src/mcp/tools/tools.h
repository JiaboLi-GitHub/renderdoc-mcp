#pragma once

namespace renderdoc::mcp { class ToolRegistry; }
namespace renderdoc::core { class Session; }

namespace renderdoc::mcp::tools {

void registerSessionTools(ToolRegistry& registry);
void registerEventTools(ToolRegistry& registry);
void registerPipelineTools(ToolRegistry& registry);
void registerExportTools(ToolRegistry& registry);
void registerInfoTools(ToolRegistry& registry);
void registerResourceTools(ToolRegistry& registry);
void registerShaderTools(ToolRegistry& registry);
void registerCaptureTools(ToolRegistry& registry);
void registerPixelTools(ToolRegistry& registry);
void registerDebugTools(ToolRegistry& registry);
void registerTexStatsTools(ToolRegistry& registry);
void registerShaderEditTools(ToolRegistry& registry);

} // namespace renderdoc::mcp::tools
