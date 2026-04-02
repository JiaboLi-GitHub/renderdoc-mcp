#pragma once

#include <stdexcept>
#include <string>

namespace renderdoc::core {

class CoreError : public std::runtime_error {
public:
    enum class Code {
        NoCaptureOpen,
        FileNotFound,
        InvalidEventId,
        InvalidResourceId,
        ReplayInitFailed,
        ExportFailed,
        InternalError,
        InvalidCoordinates,
        NoFragmentFound,
        DebugNotSupported,
        TargetNotFound,
        BuildFailed,
        UnknownShaderId,
        NoReplacementActive,
        UnknownEncoding,
        NoShaderBound,
        MeshNotAvailable,
        ImageSizeMismatch,
        ImageLoadFailed,
        InvalidPath,
        DiffNotOpen,
        DiffAlreadyOpen,
        DiffAlignmentFailed,
        MarkerNotFound
    };

    CoreError(Code code, const std::string& message)
        : std::runtime_error(message), m_code(code) {}

    Code code() const { return m_code; }

private:
    Code m_code;
};

} // namespace renderdoc::core
