#include "dpi_awareness.h"

#include <windows.h>


namespace dpi_awareness {
namespace {
constexpr int kProcessDpiUnaware = 0;
constexpr int kProcessSystemDpiAware = 1;
constexpr HRESULT kErrorNotImplemented = static_cast<HRESULT>(0x80004001L);

using GetProcessDpiAwarenessFn = HRESULT(WINAPI*)(HANDLE, int*);
using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(int);
using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
using SetProcessDPIAwareFn = BOOL(WINAPI*)();


bool QueryAwareness(HMODULE shcore, int* awareness) {
    if (shcore == nullptr || awareness == nullptr) return false;
    const auto query = reinterpret_cast<GetProcessDpiAwarenessFn>(
        GetProcAddress(shcore, "GetProcessDpiAwareness"));
    return query != nullptr && SUCCEEDED(query(GetCurrentProcess(), awareness));
}

}  // namespace

void Initialize(bool enabled) {
    if (!enabled) {
        return;
    }

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    int initial = -1;
    const bool initialKnown = QueryAwareness(shcore, &initial);
    if (initialKnown && initial != kProcessDpiUnaware) {
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
        if (result || error != ERROR_CALL_NOT_IMPLEMENTED) {
            if (shcore != nullptr) FreeLibrary(shcore);
            return;
        }
    }

    const auto setAwareness = shcore == nullptr ? nullptr :
        reinterpret_cast<SetProcessDpiAwarenessFn>(
            GetProcAddress(shcore, "SetProcessDpiAwareness"));
    if (setAwareness != nullptr) {
        const HRESULT result = setAwareness(kProcessSystemDpiAware);
        if (SUCCEEDED(result) || result != kErrorNotImplemented) {
            FreeLibrary(shcore);
            return;
        }
    }

    const auto setLegacy = user32 == nullptr ? nullptr :
        reinterpret_cast<SetProcessDPIAwareFn>(GetProcAddress(user32, "SetProcessDPIAware"));
    if (setLegacy != nullptr) {
        SetLastError(ERROR_SUCCESS);
        setLegacy();
    }
    if (shcore != nullptr) FreeLibrary(shcore);
}

}  // namespace dpi_awareness
