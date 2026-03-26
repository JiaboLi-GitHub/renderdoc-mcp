#include "mcp_server.h"
#include "renderdoc_wrapper.h"
#include "tools/tools.h"

// Default constructor: creates own wrapper + registers all tools.
// This file links against renderdoc-mcp-lib (which depends on renderdoc).
McpServer::McpServer()
    : m_ownedWrapper(std::make_unique<RenderdocWrapper>())
    , m_wrapper(m_ownedWrapper.get())
    , m_registry(&m_ownedRegistry)
{
    registerSessionTools(m_ownedRegistry);
    registerEventTools(m_ownedRegistry);
    registerPipelineTools(m_ownedRegistry);
    registerExportTools(m_ownedRegistry);
    registerInfoTools(m_ownedRegistry);
    registerResourceTools(m_ownedRegistry);
    registerShaderTools(m_ownedRegistry);
}
