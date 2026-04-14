#include "core/remote_connection.h"
#include "core/errors.h"

#include <renderdoc_replay.h>

#include <chrono>
#include <cstdio>

namespace renderdoc::core {

RemoteConnection::~RemoteConnection() {
    disconnect();
}

void RemoteConnection::connect(const std::string& url) {
    // If already connected to the same URL, just ping to verify
    if (m_server && m_url == url) {
        auto pingResult = m_server->Ping();
        if (pingResult.OK())
            return; // Still alive
        // Dead connection — clean up and reconnect
        disconnect();
    } else if (m_server) {
        disconnect();
    }

    IRemoteServer* server = nullptr;
    auto result = RENDERDOC_CreateRemoteServerConnection(rdcstr(url.c_str()), &server);
    if (!result.OK() || !server) {
        throw CoreError(CoreError::Code::RemoteConnectionFailed,
                        "Failed to connect to remote server at " + url + ": " +
                        std::string(result.Message().c_str()));
    }

    m_server = server;
    m_url = url;
    startPing();

    std::fprintf(stderr, "[renderdoc-mcp] Connected to remote server: %s\n", url.c_str());
}

void RemoteConnection::disconnect() {
    stopPing();

    if (m_server) {
        m_server->ShutdownConnection();
        m_server = nullptr;
    }
    m_url.clear();
}

bool RemoteConnection::isConnected() const {
    return m_server != nullptr;
}

IRemoteServer* RemoteConnection::server() const {
    return m_server;
}

std::string RemoteConnection::copyCapture(const std::string& localPath) {
    if (!m_server)
        throw CoreError(CoreError::Code::RemoteConnectionFailed, "Not connected to remote server");

    rdcstr remotePath = m_server->CopyCaptureToRemote(rdcstr(localPath.c_str()), nullptr);
    if (remotePath.empty())
        throw CoreError(CoreError::Code::RemoteConnectionFailed,
                        "Failed to copy capture to remote server: " + localPath);

    std::fprintf(stderr, "[renderdoc-mcp] Uploaded capture to remote: %s\n",
                 std::string(remotePath.c_str()).c_str());
    return std::string(remotePath.c_str());
}

IReplayController* RemoteConnection::openCapture(const std::string& remotePath) {
    if (!m_server)
        throw CoreError(CoreError::Code::RemoteConnectionFailed, "Not connected to remote server");

    ReplayOptions opts;
    auto [status, controller] = m_server->OpenCapture(
        ~0U, // NoPreference — let server choose the best local proxy
        rdcstr(remotePath.c_str()),
        opts,
        nullptr // No progress callback
    );

    if (!status.OK() || !controller) {
        throw CoreError(CoreError::Code::ReplayInitFailed,
                        "Failed to open remote replay: " +
                        std::string(status.Message().c_str()));
    }

    std::fprintf(stderr, "[renderdoc-mcp] Remote capture opened successfully\n");
    return controller;
}

void RemoteConnection::closeCapture(IReplayController* ctrl) {
    if (ctrl && m_server) {
        m_server->CloseCapture(ctrl);
    }
}

void RemoteConnection::startPing() {
    stopPing(); // Ensure no existing ping thread
    m_pingRunning = true;
    m_pingThread = std::thread([this]() {
        while (m_pingRunning.load()) {
            // Sleep in small increments so we can exit quickly
            for (int i = 0; i < 30 && m_pingRunning.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

            if (!m_pingRunning.load())
                break;

            if (m_server) {
                auto result = m_server->Ping();
                if (!result.OK()) {
                    std::fprintf(stderr, "[renderdoc-mcp] Remote ping failed — connection may be lost\n");
                    break;
                }
            }
        }
    });
}

void RemoteConnection::stopPing() {
    m_pingRunning = false;
    if (m_pingThread.joinable())
        m_pingThread.join();
}

} // namespace renderdoc::core
