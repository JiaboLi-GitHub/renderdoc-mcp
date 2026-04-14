#pragma once

#include "core/types.h"
#include "core/shader_edit.h"
#include "core/remote_connection.h"
#include <string>

// Forward declarations from RenderDoc
struct ICaptureFile;
struct IReplayController;

namespace renderdoc::core {

class Session {
public:
    Session();
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Public API
    CaptureInfo open(const std::string& path);
    void close();
    SessionStatus status() const;
    bool isOpen() const;
    void ensureReplayInitialized();

    // Remote replay configuration — call before open().
    void setRemoteUrl(const std::string& url);
    bool isRemoteMode() const;

    // Internal accessors for other core modules.
    // Convention: mcp/cli layers should NOT call these directly.
    IReplayController* controller() const;
    ICaptureFile* captureFile() const;
    uint32_t currentEventId() const;
    const std::string& capturePath() const;
    std::string exportDir() const;
    ShaderEditState& shaderEditState();
    const ShaderEditState& shaderEditState() const;

private:
    friend EventInfo gotoEvent(Session& session, uint32_t eventId);

    void setCurrentEventId(uint32_t eid);
    void closeCurrent();

    ICaptureFile* m_captureFile = nullptr;
    IReplayController* m_controller = nullptr;
    uint32_t m_currentEventId = 0;
    std::string m_capturePath;
    bool m_replayInitialized = false;
    uint32_t m_totalEvents = 0;
    GraphicsApi m_api = GraphicsApi::Unknown;
    ShaderEditState m_shaderEditState;

    // Remote replay state
    RemoteConnection m_remote;
    std::string m_remoteUrl;
    std::string m_remotePath;
    bool m_isRemote = false;
};

} // namespace renderdoc::core
