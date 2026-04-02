#include "mcp/mcp_server.h"
#include "mcp/tool_registry.h"
#include "mcp/tools/tools.h"
#include "core/session.h"
#include "core/diff_session.h"

namespace renderdoc::mcp {

McpServer::McpServer() {
    m_ownedSession = std::make_unique<core::Session>();
    m_session = m_ownedSession.get();

    m_ownedDiffSession = std::make_unique<core::DiffSession>();
    m_diffSession = m_ownedDiffSession.get();

    m_ownedRegistry = std::make_unique<ToolRegistry>();
    m_registry = m_ownedRegistry.get();

    tools::registerSessionTools(*m_registry);
    tools::registerEventTools(*m_registry);
    tools::registerInfoTools(*m_registry);
    tools::registerResourceTools(*m_registry);
    tools::registerShaderTools(*m_registry);
    tools::registerPipelineTools(*m_registry);
    tools::registerExportTools(*m_registry);
    tools::registerCaptureTools(*m_registry);
    tools::registerPixelTools(*m_registry);
    tools::registerDebugTools(*m_registry);
    tools::registerTexStatsTools(*m_registry);
    tools::registerShaderEditTools(*m_registry);
    tools::registerMeshTools(*m_registry);
    tools::registerSnapshotTools(*m_registry);
    tools::registerUsageTools(*m_registry);
    tools::registerAssertionTools(*m_registry);
}

} // namespace renderdoc::mcp
