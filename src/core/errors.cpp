#include "core/errors.h"

// CoreError destructor is inline (= default in class body).
// On MSVC, vtable emission with inline destructors works correctly.
// This TU exists to ensure the CMake target always has at least one .cpp file
// and to provide a future anchor point if cross-platform needs arise.
namespace renderdoc::core {} // namespace renderdoc::core
