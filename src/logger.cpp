#include "logger.h"

#include <windows.h>
#include <shlwapi.h>

#include <cstdarg>
#include <cstdio>

namespace logger {
namespace {
HANDLE g_file = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_lock;
bool g_lockReady = false;

bool CreateSessionFile() {
    wchar_t directory[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH || !PathRemoveFileSpecW(directory)) return false;

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    for (unsigned int suffix = 0; suffix < 100; ++suffix) {
        wchar_t name[128] = {};
        if (suffix == 0) {
            wsprintfW(name, L"BHD_QoL_%04u-%02u-%02u_%02u-%02u-%02u_%lu.log", now.wYear,
                      now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
                      GetCurrentProcessId());
        } else {
            wsprintfW(name, L"BHD_QoL_%04u-%02u-%02u_%02u-%02u-%02u_%lu_%u.log", now.wYear,
                      now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
                      GetCurrentProcessId(), suffix);
        }
        wchar_t path[MAX_PATH] = {};
        lstrcpynW(path, directory, MAX_PATH);
        if (!PathAppendW(path, name)) return false;
        g_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g_file != INVALID_HANDLE_VALUE) return true;
        if (GetLastError() != ERROR_FILE_EXISTS) return false;
    }
    return false;
}
}  // namespace

bool Initialize(bool enabled) {
    if (!enabled) return false;
    InitializeCriticalSection(&g_lock);
    g_lockReady = true;
    if (!CreateSessionFile()) {
        DeleteCriticalSection(&g_lock);
        g_lockReady = false;
        return false;
    }
    Log("INFO", "Startup", "BHD_QoL diagnostic session started; pid=%lu", GetCurrentProcessId());
    return true;
}

bool IsEnabled() {
    return g_file != INVALID_HANDLE_VALUE;
}

void Log(const char* severity, const char* category, const char* format, ...) {
    if (!g_lockReady || g_file == INVALID_HANDLE_VALUE) return;

    char message[1536] = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnprintf(message, sizeof(message) - 1, format, arguments);
    va_end(arguments);

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    char line[2048] = {};
    _snprintf(line, sizeof(line) - 1,
              "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [%s] [%s] [tid=%lu] %s\r\n",
              now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
              now.wMilliseconds, severity, category, GetCurrentThreadId(), message);

    EnterCriticalSection(&g_lock);
    DWORD written = 0;
    WriteFile(g_file, line, static_cast<DWORD>(lstrlenA(line)), &written, nullptr);
    FlushFileBuffers(g_file);
    LeaveCriticalSection(&g_lock);
}
}  // namespace logger
