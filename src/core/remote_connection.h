#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <utility>

// Forward declarations from RenderDoc
struct IRemoteServer;
struct IReplayController;
struct ResultDetails;

namespace renderdoc::core {

/// Manages a connection to a remote RenderDoc replay server (renderdoccmd remoteserver).
/// Handles connect/disconnect, file transfer, capture open/close, and keepalive ping.
/// Used by both Session and DiffSession to support remote replay on macOS and other
/// platforms without local GPU replay capability.
class RemoteConnection {
public:
    RemoteConnection() = default;
    ~RemoteConnection();

    RemoteConnection(const RemoteConnection&) = delete;
    RemoteConnection& operator=(const RemoteConnection&) = delete;

    /// Connect to a remote RenderDoc server at the given URL (e.g. "host:39920").
    /// Throws CoreError(RemoteConnectionFailed) on failure.
    void connect(const std::string& url);

    /// Disconnect from the remote server. Stops ping thread and shuts down connection.
    /// Safe to call if not connected.
    void disconnect();

    /// Returns true if currently connected to a remote server.
    bool isConnected() const;

    /// Returns the remote server pointer. nullptr if not connected.
    IRemoteServer* server() const;

    /// Copy a local .rdc file to the remote server.
    /// Returns the remote path where the file was stored.
    /// Throws CoreError on failure.
    std::string copyCapture(const std::string& localPath);

    /// Open a capture on the remote server and return the replay controller.
    /// The remotePath should be obtained from copyCapture().
    /// Returns the controller pointer (caller does NOT call Shutdown on it — use closeCapture instead).
    /// Throws CoreError on failure.
    IReplayController* openCapture(const std::string& remotePath);

    /// Close a capture opened via openCapture(). Uses CloseCapture (not Shutdown).
    /// Safe to call with nullptr.
    void closeCapture(IReplayController* ctrl);

private:
    void startPing();
    void stopPing();

    IRemoteServer* m_server = nullptr;
    std::string m_url;
    std::atomic<bool> m_pingRunning{false};
    std::thread m_pingThread;
};

} // namespace renderdoc::core
