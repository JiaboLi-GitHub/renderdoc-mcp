#include "server_log.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#else
#include <unistd.h>
#endif

namespace renderdoc::mcp::serverlog {

namespace {

FILE* g_file = nullptr;
std::string g_path;

void writeTimestamp(FILE* f)
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    int ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    std::fprintf(f, "%s.%03d ", buf, ms);
}

unsigned long currentPid()
{
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}

void onTerminate()
{
    std::string reason = "std::terminate called";
    if(auto ex = std::current_exception())
    {
        try
        {
            std::rethrow_exception(ex);
        }
        catch(const std::exception& e)
        {
            reason += " with uncaught exception: ";
            reason += e.what();
        }
        catch(...)
        {
            reason += " with uncaught non-standard exception";
        }
    }
    write("FATAL", reason);
    std::abort();
}

#ifdef _WIN32
const char* exceptionName(DWORD code)
{
    switch(code)
    {
        case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
        case STATUS_HEAP_CORRUPTION: return "STATUS_HEAP_CORRUPTION";
        case STATUS_STACK_BUFFER_OVERRUN: return "STATUS_STACK_BUFFER_OVERRUN";
        case 0xE06D7363: return "C++ exception";
        default: return "native exception";
    }
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* info)
{
    write("FATAL", "Unhandled " + describeNativeException(info));
    std::string dump = writeMinidump(info);
    write("FATAL", dump.empty() ? "Failed to write minidump" : "Minidump written to " + dump);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

} // namespace

bool open(const std::string& logPath)
{
    if(g_file)
        return true;
    g_file = std::fopen(logPath.c_str(), "a");
    if(!g_file)
        return false;
    g_path = logPath;
    return true;
}

bool enabled()
{
    return g_file != nullptr;
}

const std::string& path()
{
    return g_path;
}

void write(const char* level, const std::string& message)
{
    if(!g_file)
        return;
    writeTimestamp(g_file);
    std::fprintf(g_file, "[%lu] %-5s %s\n", currentPid(), level, message.c_str());
    // Flush every line so the log is complete even if the process dies right after.
    std::fflush(g_file);
}

void installCrashHandlers()
{
    if(!g_file)
        return;
    std::set_terminate(onTerminate);
    reassertCrashHandlers();
}

void reassertCrashHandlers()
{
#ifdef _WIN32
    if(g_file)
        SetUnhandledExceptionFilter(onUnhandledException);
#endif
}

#ifdef _WIN32
std::string describeNativeException(const _EXCEPTION_POINTERS* info)
{
    const EXCEPTION_RECORD* rec = info->ExceptionRecord;
    char buf[MAX_PATH + 160];
    int len = std::snprintf(buf, sizeof(buf), "%s (0x%08lX)", exceptionName(rec->ExceptionCode),
                            static_cast<unsigned long>(rec->ExceptionCode));

    if(rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
    {
        const char* op = rec->ExceptionInformation[0] == 0   ? "reading"
                         : rec->ExceptionInformation[0] == 8 ? "executing"
                                                             : "writing";
        len += std::snprintf(buf + len, sizeof(buf) - len, " %s address 0x%llX", op,
                             static_cast<unsigned long long>(rec->ExceptionInformation[1]));
    }

    HMODULE module = nullptr;
    if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          static_cast<LPCSTR>(rec->ExceptionAddress), &module))
    {
        char modulePath[MAX_PATH] = {};
        GetModuleFileNameA(module, modulePath, MAX_PATH);
        const char* name = modulePath;
        for(const char* p = modulePath; *p; ++p)
            if(*p == '\\' || *p == '/')
                name = p + 1;
        std::snprintf(buf + len, sizeof(buf) - len, " at %s+0x%llX", name,
                      static_cast<unsigned long long>(
                          reinterpret_cast<uintptr_t>(rec->ExceptionAddress) -
                          reinterpret_cast<uintptr_t>(module)));
    }
    else
    {
        std::snprintf(buf + len, sizeof(buf) - len, " at 0x%llX",
                      static_cast<unsigned long long>(
                          reinterpret_cast<uintptr_t>(rec->ExceptionAddress)));
    }
    return buf;
}

std::string writeMinidump(_EXCEPTION_POINTERS* info)
{
    if(!g_file)
        return {};

    static std::atomic<unsigned> s_dumpIndex{0};
    std::string dumpPath = g_path + "." + std::to_string(currentPid()) + "." +
                           std::to_string(++s_dumpIndex) + ".dmp";
    HANDLE file = CreateFileA(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if(file == INVALID_HANDLE_VALUE)
        return {};

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = info;
    mei.ClientPointers = FALSE;
    auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory |
                                           MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &mei,
                                nullptr, nullptr);
    CloseHandle(file);
    return ok ? dumpPath : std::string();
}
#endif

} // namespace renderdoc::mcp::serverlog
