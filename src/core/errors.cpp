#include "core/errors.h"

// CoreError is fully inline in the header.
// This translation unit exists so the CMake target always has at least one .cpp
// and to anchor the vtable in a single TU.
namespace renderdoc::core {
// intentionally empty - vtable anchored by out-of-line destructor if needed
} // namespace renderdoc::core
