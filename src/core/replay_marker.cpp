// Prevent RenderDoc from trying to capture this replay process.
// Any executable that uses the RenderDoc replay API must export this marker
// so that renderdoc.dll recognizes it as a replay tool and does not attempt
// to hook/capture it (which would cause crashes in OpenCapture).
#include <renderdoc_replay.h>
REPLAY_PROGRAM_MARKER()
