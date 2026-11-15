#pragma once

#include <string>

#ifdef _WIN32
struct _EXCEPTION_POINTERS;
#endif

// Optional diagnostic log for the renderdoc-mcp server, enabled with --log-file or RENDERDOC_MCP_LOG.
namespace renderdoc::mcp::serverlog {

bool open(const std::string& path);
bool enabled();
const std::string& path();
void write(const char* level, const std::string& message);

// Log std::terminate and unhandled native exceptions (with a minidump next to the log on Windows).
void installCrashHandlers();

// RenderDoc may replace the process-wide crash filter when it initialises replay, so re-assert ours.
void reassertCrashHandlers();

#ifdef _WIN32
// e.g. "EXCEPTION_ACCESS_VIOLATION (0xC0000005) reading address 0x0 at renderdoc.dll+0x1A2B3C"
std::string describeNativeException(const _EXCEPTION_POINTERS* info);

// Writes <log path>.<pid>.dmp; returns the dump path, or empty if logging is off or writing failed.
std::string writeMinidump(_EXCEPTION_POINTERS* info);
#endif

} // namespace renderdoc::mcp::serverlog
