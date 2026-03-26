#pragma once

class ToolRegistry;

// Each tools/*.cpp provides a registration function
void registerSessionTools(ToolRegistry& registry);
void registerEventTools(ToolRegistry& registry);
void registerPipelineTools(ToolRegistry& registry);
void registerExportTools(ToolRegistry& registry);
void registerInfoTools(ToolRegistry& registry);
void registerResourceTools(ToolRegistry& registry);
void registerShaderTools(ToolRegistry& registry);
