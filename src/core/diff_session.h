#pragma once

#include "core/types.h"
#include <string>

struct ICaptureFile;
struct IReplayController;

namespace renderdoc::core {

class DiffSession {
public:
    DiffSession();
    ~DiffSession();

    DiffSession(const DiffSession&) = delete;
    DiffSession& operator=(const DiffSession&) = delete;

    struct OpenResult {
        CaptureInfo infoA;
        CaptureInfo infoB;
    };

    OpenResult open(const std::string& pathA, const std::string& pathB);
    void close();
    bool isOpen() const;

    IReplayController* controllerA() const;
    IReplayController* controllerB() const;
    ICaptureFile* captureFileA() const;
    ICaptureFile* captureFileB() const;
    const std::string& pathA() const;
    const std::string& pathB() const;

private:
    ICaptureFile* m_capA = nullptr;
    ICaptureFile* m_capB = nullptr;
    IReplayController* m_ctrlA = nullptr;
    IReplayController* m_ctrlB = nullptr;
    std::string m_pathA, m_pathB;

    CaptureInfo openOne(const std::string& path, ICaptureFile*& cap, IReplayController*& ctrl);
    void closeOne(ICaptureFile*& cap, IReplayController*& ctrl);
};

} // namespace renderdoc::core
