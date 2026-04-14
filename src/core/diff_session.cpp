#include "core/diff_session.h"
#include "core/action_helpers.h"
#include "core/errors.h"

// RenderDoc headers — guarded by RENDERDOC_DIR at build time
#include <renderdoc_replay.h>

namespace renderdoc::core {

DiffSession::DiffSession() = default;

DiffSession::~DiffSession() {
    close();
    m_remote.disconnect();
}

void DiffSession::setRemoteUrl(const std::string& url) {
    m_remoteUrl = url;
}

bool DiffSession::isRemoteMode() const {
    return !m_remoteUrl.empty();
}

CaptureInfo DiffSession::openOne(const std::string& path, ICaptureFile*& cap, IReplayController*& ctrl) {
    if (isRemoteMode()) {
        // ── Remote replay path ──────────────────────────────────
        if (!m_remote.isConnected())
            m_remote.connect(m_remoteUrl);

        std::string remotePath = m_remote.copyCapture(path);
        ctrl = m_remote.openCapture(remotePath);

        // Store remote path for cleanup
        if (&ctrl == &m_ctrlA)
            m_remotePathA = remotePath;
        else
            m_remotePathB = remotePath;

        // Try to open local CaptureFile for metadata (non-fatal if fails)
        cap = RENDERDOC_OpenCaptureFile();
        if (cap) {
            auto fileStatus = cap->OpenFile(rdcstr(path.c_str()), "", nullptr);
            if (!fileStatus.OK()) {
                cap->Shutdown();
                cap = nullptr;
            }
        }
    } else {
        // ── Local replay path (existing code) ───────────────────
        cap = RENDERDOC_OpenCaptureFile();
        if (!cap)
            throw CoreError(CoreError::Code::InternalError, "Failed to create capture file object");

        auto status = cap->OpenFile(rdcstr(path.c_str()), "", nullptr);
        if (!status.OK()) {
            cap->Shutdown();
            cap = nullptr;
            throw CoreError(CoreError::Code::FileNotFound,
                            "Failed to open capture: " + std::string(status.Message().c_str()));
        }

        ReplayOptions opts;
        auto [replayStatus, controller] = cap->OpenCapture(opts, nullptr);
        if (!replayStatus.OK() || !controller) {
            cap->Shutdown();
            cap = nullptr;
            throw CoreError(CoreError::Code::ReplayInitFailed,
                            "Failed to open replay: " + std::string(replayStatus.Message().c_str()));
        }

        ctrl = controller;
    }

    // ── Common: gather metadata from the controller ─────────────
    auto apiProps = ctrl->GetAPIProperties();
    GraphicsApi api = toGraphicsApi(apiProps.pipelineType);

    const auto& rootActions = ctrl->GetRootActions();
    uint32_t totalEvents = 0;
    uint32_t totalDraws = 0;
    for (const auto& action : rootActions) {
        totalEvents += countAllEvents(action);
        totalDraws += countDrawCalls(action);
    }

    CaptureInfo info;
    info.path = path;
    info.api = api;
    info.degraded = apiProps.degraded;
    info.totalEvents = totalEvents;
    info.totalDraws = totalDraws;

    return info;
}

void DiffSession::closeOne(ICaptureFile*& cap, IReplayController*& ctrl) {
    if (ctrl) {
        if (m_isRemote) {
            m_remote.closeCapture(ctrl);
        } else {
            ctrl->Shutdown();
        }
        ctrl = nullptr;
    }
    if (cap) {
        cap->Shutdown();
        cap = nullptr;
    }
}

DiffSession::OpenResult DiffSession::open(const std::string& pathA, const std::string& pathB) {
    if (isOpen())
        throw CoreError(CoreError::Code::DiffAlreadyOpen, "A diff session is already open");

    // Ensure replay subsystem is initialized before opening captures.
    if (!m_replayInitialized) {
        GlobalEnvironment env;
        memset(&env, 0, sizeof(env));
        rdcarray<rdcstr> args;
        RENDERDOC_InitialiseReplay(env, args);
        m_replayInitialized = true;
    }

    m_isRemote = isRemoteMode();

    OpenResult result;

    // Open capture A first
    result.infoA = openOne(pathA, m_capA, m_ctrlA);
    m_pathA = pathA;

    // Open capture B — if it fails, close A before rethrowing
    try {
        result.infoB = openOne(pathB, m_capB, m_ctrlB);
        m_pathB = pathB;
    } catch (...) {
        closeOne(m_capA, m_ctrlA);
        m_pathA.clear();
        throw;
    }

    return result;
}

void DiffSession::close() {
    closeOne(m_capB, m_ctrlB);
    closeOne(m_capA, m_ctrlA);
    m_pathA.clear();
    m_pathB.clear();
    m_remotePathA.clear();
    m_remotePathB.clear();
    m_isRemote = false;
}

bool DiffSession::isOpen() const {
    return m_ctrlA != nullptr && m_ctrlB != nullptr;
}

IReplayController* DiffSession::controllerA() const {
    if (!m_ctrlA)
        throw CoreError(CoreError::Code::DiffNotOpen, "No diff session is currently open");
    return m_ctrlA;
}

IReplayController* DiffSession::controllerB() const {
    if (!m_ctrlB)
        throw CoreError(CoreError::Code::DiffNotOpen, "No diff session is currently open");
    return m_ctrlB;
}

ICaptureFile* DiffSession::captureFileA() const {
    if (!m_capA) {
        if (m_isRemote)
            return nullptr;
        throw CoreError(CoreError::Code::DiffNotOpen, "No diff session is currently open");
    }
    return m_capA;
}

ICaptureFile* DiffSession::captureFileB() const {
    if (!m_capB) {
        if (m_isRemote)
            return nullptr;
        throw CoreError(CoreError::Code::DiffNotOpen, "No diff session is currently open");
    }
    return m_capB;
}

const std::string& DiffSession::pathA() const { return m_pathA; }
const std::string& DiffSession::pathB() const { return m_pathB; }

} // namespace renderdoc::core
