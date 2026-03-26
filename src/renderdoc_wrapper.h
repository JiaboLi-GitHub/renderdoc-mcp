#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

// Forward declarations - actual types come from renderdoc headers
struct ICaptureFile;
struct IReplayController;

class RenderdocWrapper
{
public:
    RenderdocWrapper() = default;
    ~RenderdocWrapper();

    // Non-copyable
    RenderdocWrapper(const RenderdocWrapper&) = delete;
    RenderdocWrapper& operator=(const RenderdocWrapper&) = delete;

    // Core operations - return JSON result or throw std::runtime_error
    nlohmann::json openCapture(const std::string& path);
    nlohmann::json listEvents(const std::string& filter);
    nlohmann::json gotoEvent(uint32_t eventId);
    nlohmann::json getPipelineState();
    nlohmann::json exportRenderTarget(int index);

    void shutdown();

    bool hasCaptureOpen() const { return m_controller != nullptr; }

private:
    void ensureReplayInitialized();
    void closeCurrent();
    std::string getExportDir() const;
    std::string generateOutputPath(uint32_t eventId, int index) const;

    ICaptureFile* m_captureFile = nullptr;
    IReplayController* m_controller = nullptr;
    uint32_t m_currentEventId = 0;
    std::string m_capturePath;
    bool m_replayInitialized = false;
};
