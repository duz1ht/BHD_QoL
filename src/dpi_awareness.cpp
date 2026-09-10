#include "dpi_awareness.h"

#include <windows.h>

#include "logger.h"

namespace dpi_awareness {
namespace {
constexpr int kProcessDpiUnaware = 0;
constexpr int kProcessSystemDpiAware = 1;
constexpr int kProcessPerMonitorDpiAware = 2;
constexpr HRESULT kErrorNotImplemented = static_cast<HRESULT>(0x80004001L);

using GetProcessDpiAwarenessFn = HRESULT(WINAPI*)(HANDLE, int*);
using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(int);
using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
using SetProcessDPIAwareFn = BOOL(WINAPI*)();

const char* AwarenessName(int awareness) {
    switch (awareness) {
        case kProcessDpiUnaware: return "unaware";
        case kProcessSystemDpiAware: return "system_aware";
        case kProcessPerMonitorDpiAware: return "per_monitor_aware";
        default: return "unknown";
    }
}

bool QueryAwareness(HMODULE shcore, int* awareness) {
    if (shcore == nullptr || awareness == nullptr) return false;
    const auto query = reinterpret_cast<GetProcessDpiAwarenessFn>(
        GetProcAddress(shcore, "GetProcessDpiAwareness"));
    return query != nullptr && SUCCEEDED(query(GetCurrentProcess(), awareness));
}

void LogEffectiveAwareness(HMODULE shcore) {
    int effective = -1;
    if (QueryAwareness(shcore, &effective)) {
        logger::Log("INFO", "DPI", "effective_awareness=%s", AwarenessName(effective));
    } else {
        logger::Log("INFO", "DPI", "effective_awareness=unknown query_unavailable=1");
    }
}
}  // namespace

void Initialize(bool enabled) {
    if (!enabled) {
        logger::Log("INFO", "DPI", "feature disabled");
        return;
    }

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    int initial = -1;
    const bool initialKnown = QueryAwareness(shcore, &initial);
    logger::Log("INFO", "DPI", "initial_awareness=%s requested=system_aware",
                initialKnown ? AwarenessName(initial) : "unknown");
    if (initialKnown && initial != kProcessDpiUnaware) {
        logger::Log("INFO", "DPI", "operation=skipped reason=process_already_aware");
        LogEffectiveAwareness(shcore);
        if (shcore != nullptr) FreeLibrary(shcore);
        return;
    }

    const auto setContext = user32 == nullptr ? nullptr :
        reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setContext != nullptr) {
        SetLastError(ERROR_SUCCESS);
        // DPI_AWARENESS_CONTEXT_SYSTEM_AWARE is the pseudo-handle -2.
        const BOOL result = setContext(reinterpret_cast<HANDLE>(-2));
        const DWORD error = result ? ERROR_SUCCESS : GetLastError();
        logger::Log(result ? "INFO" : "ERROR", "DPI",
                    "api=SetProcessDpiAwarenessContext result=%d error=%lu",
                    result, error);
        if (result || error != ERROR_CALL_NOT_IMPLEMENTED) {
            LogEffectiveAwareness(shcore);
            if (shcore != nullptr) FreeLibrary(shcore);
            return;
        }
    }

    const auto setAwareness = shcore == nullptr ? nullptr :
        reinterpret_cast<SetProcessDpiAwarenessFn>(
            GetProcAddress(shcore, "SetProcessDpiAwareness"));
    if (setAwareness != nullptr) {
        const HRESULT result = setAwareness(kProcessSystemDpiAware);
        logger::Log(SUCCEEDED(result) ? "INFO" : "ERROR", "DPI",
                    "api=SetProcessDpiAwareness result=0x%08lX",
                    static_cast<unsigned long>(result));
        if (SUCCEEDED(result) || result != kErrorNotImplemented) {
            LogEffectiveAwareness(shcore);
            FreeLibrary(shcore);
            return;
        }
    }

    const auto setLegacy = user32 == nullptr ? nullptr :
        reinterpret_cast<SetProcessDPIAwareFn>(GetProcAddress(user32, "SetProcessDPIAware"));
    if (setLegacy != nullptr) {
        SetLastError(ERROR_SUCCESS);
        const BOOL result = setLegacy();
        logger::Log(result ? "INFO" : "ERROR", "DPI",
                    "api=SetProcessDPIAware result=%d error=%lu",
                    result, result ? ERROR_SUCCESS : GetLastError());
    } else {
        logger::Log("INFO", "DPI", "operation=skipped reason=no_supported_api");
    }
    LogEffectiveAwareness(shcore);
    if (shcore != nullptr) FreeLibrary(shcore);
}

}  // namespace dpi_awareness
