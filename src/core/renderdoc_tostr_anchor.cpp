// Provide template specializations required by renderdoc_tostr.inl macros.
// This must be compiled into exactly one translation unit in renderdoc-core.
// Without it, any code that uses RenderDoc enums (e.g. ResultCode) in string
// conversions will get linker errors for DoStringise<T>.

#include <renderdoc_replay.h>
#include <stringise.h>

template <>
rdcstr DoStringise(const uint32_t &el)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", el);
    return rdcstr(buf);
}

#include "renderdoc_tostr.inl"
