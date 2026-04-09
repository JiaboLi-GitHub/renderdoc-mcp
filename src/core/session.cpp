#include "core/session.h"
#include "core/action_helpers.h"
#include "core/shader_edit.h"
#include "core/errors.h"

// RenderDoc headers — guarded by RENDERDOC_DIR at build time
#include <renderdoc_replay.h>

namespace renderdoc::core {

Session::Session() = default;

Session::~Session() {
    close();
    if (m_replayInitialized)
        RENDERDOC_ShutdownReplay();
}

void Session::ensureReplayInitialized() {
    if (m_replayInitialized)
        return;
    GlobalEnvironment env;
    memset(&env, 0, sizeof(env));
    rdcarray<rdcstr> args;
    RENDERDOC_InitialiseReplay(env, args);
    m_replayInitialized = true;
}

void Session::closeCurrent() {
    if (m_controller) {
        cleanupShaderEdits(*this);
        m_controller->Shutdown();
        m_controller = nullptr;
    }
    if (m_captureFile) {
        m_captureFile->Shutdown();
        m_captureFile = nullptr;
    }
    m_currentEventId = 0;
    m_capturePath.clear();
    m_totalEvents = 0;
    m_api = GraphicsApi::Unknown;
    m_shaderEditState.clear();
}

void Session::close() {
    closeCurrent();
}

CaptureInfo Session::open(const std::string& path) {
    ensureReplayInitialized();
    closeCurrent();

    m_captureFile = RENDERDOC_OpenCaptureFile();
    if (!m_captureFile)
        throw CoreError(CoreError::Code::InternalError, "Failed to create capture file object");

    auto status = m_captureFile->OpenFile(rdcstr(path.c_str()), "", nullptr);
    if (!status.OK()) {
        m_captureFile->Shutdown();
        m_captureFile = nullptr;
        throw CoreError(CoreError::Code::FileNotFound,
                        "Failed to open capture: " + std::string(status.Message().c_str()));
    }

    ReplayOptions opts;
    auto [replayStatus, controller] = m_captureFile->OpenCapture(opts, nullptr);
    if (!replayStatus.OK() || !controller) {
        m_captureFile->Shutdown();
        m_captureFile = nullptr;
        throw CoreError(CoreError::Code::ReplayInitFailed,
                        "Failed to open replay: " + std::string(replayStatus.Message().c_str()));
    }

    m_controller = controller;
    m_capturePath = path;

    // Gather metadata
    auto apiProps = m_controller->GetAPIProperties();
    m_api = toGraphicsApi(apiProps.pipelineType);

    const auto& rootActions = m_controller->GetRootActions();
    m_totalEvents = 0;
    uint32_t totalDraws = 0;
    for (const auto& action : rootActions) {
        m_totalEvents += countAllEvents(action);
        totalDraws += countDrawCalls(action);
    }

    CaptureInfo info;
    info.path = path;
    info.api = m_api;
    info.degraded = apiProps.degraded;
    info.totalEvents = m_totalEvents;
    info.totalDraws = totalDraws;

    return info;
}

SessionStatus Session::status() const {
    SessionStatus s;
    s.isOpen = isOpen();
    s.capturePath = m_capturePath;
    s.api = m_api;
    s.currentEventId = m_currentEventId;
    s.totalEvents = m_totalEvents;
    return s;
}

bool Session::isOpen() const {
    return m_controller != nullptr;
}

IReplayController* Session::controller() const {
    if (!m_controller)
        throw CoreError(CoreError::Code::NoCaptureOpen, "No capture is currently open");
    return m_controller;
}

ICaptureFile* Session::captureFile() const {
    if (!m_captureFile)
        throw CoreError(CoreError::Code::NoCaptureOpen, "No capture is currently open");
    return m_captureFile;
}

uint32_t Session::currentEventId() const { return m_currentEventId; }
const std::string& Session::capturePath() const { return m_capturePath; }

std::string Session::exportDir() const {
    if (m_capturePath.empty()) return ".";
    auto pos = m_capturePath.find_last_of("\\/");
    std::string dir = (pos != std::string::npos) ? m_capturePath.substr(0, pos) : ".";
    return dir + "/renderdoc-mcp-export";
}

void Session::setCurrentEventId(uint32_t eid) {
    m_currentEventId = eid;
}

ShaderEditState& Session::shaderEditState() { return m_shaderEditState; }
const ShaderEditState& Session::shaderEditState() const { return m_shaderEditState; }

} // namespace renderdoc::core
