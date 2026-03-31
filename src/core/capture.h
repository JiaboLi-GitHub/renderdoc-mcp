#pragma once

#include "core/types.h"
#include <string>

namespace renderdoc::core {

class Session;

CaptureResult captureFrame(Session& session, const CaptureRequest& req);

} // namespace renderdoc::core
