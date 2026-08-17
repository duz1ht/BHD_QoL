#include "cursor_clip_recovery.h"

#include <atomic>
#include <cstring>

namespace cursor_clip_recovery {
namespace {

constexpr UINT_PTR kRecoveryTimer = 0xB4D01;

using ClipCursorFn = BOOL(WINAPI*)(const RECT*);

struct RecoveryState {
    std::atomic<bool> clipActive{false};
    std::atomic<bool> clipObserved{false};
    std::atomic<bool> recovering{false};
    std::atomic<bool> focusReturned{false};
    std::atomic<bool> restoreExpected{false};
    std::atomic<bool> clipAppliedByUs{false};
    std::atomic<LONG> left{0};
    std::atomic<LONG> top{0};
    std::atomic<LONG> right{0};
    std::atomic<LONG> bottom{0};
    std::atomic<UINT> displayWidth{0};
    std::atomic<UINT> displayHeight{0};
};

Settings g_settings;
RecoveryState g_state;
HWND g_window = nullptr;
WNDPROC g_originalWndProc = nullptr;
void** g_clipCursorSlot = nullptr;
ClipCursorFn g_clipCursor = nullptr;
thread_local bool g_insideClipHook = false;

bool RectsEqual(const RECT& left, const RECT& right) {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
}

RECT ExpectedClip() {
    return {g_state.left.load(), g_state.top.load(),
            g_state.right.load(), g_state.bottom.load()};
}

void RecordClip(const RECT* requested, BOOL succeeded) {
    if (!succeeded) return;

    g_state.clipObserved = true;
    g_state.clipActive = requested != nullptr;
    if (!requested) return;

    RECT actual{};
    if (!GetClipCursor(&actual)) return;
    g_state.left = actual.left;
    g_state.top = actual.top;
    g_state.right = actual.right;
    g_state.bottom = actual.bottom;
}

BOOL WINAPI HookClipCursor(const RECT* rect) {
    if (g_insideClipHook) return g_clipCursor(rect);
    g_insideClipHook = true;
    const BOOL result = g_clipCursor(rect);
    RecordClip(rect, result);
    g_insideClipHook = false;
    return result;
}

bool PatchClipCursorImport() {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const auto& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress) return false;

    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        const char* moduleName = reinterpret_cast<const char*>(base + descriptor->Name);
        if (_stricmp(moduleName, "USER32.dll") != 0) continue;

        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        auto* names = descriptor->OriginalFirstThunk
            ? reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk) : thunk;
        for (; names->u1.AddressOfData; ++names, ++thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import->Name), "ClipCursor") != 0) continue;

            auto** slot = reinterpret_cast<void**>(&thunk->u1.Function);
            DWORD oldProtect = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) return false;
            g_clipCursor = reinterpret_cast<ClipCursorFn>(*slot);
            g_clipCursorSlot = slot;
            *slot = reinterpret_cast<void*>(HookClipCursor);
            DWORD ignored = 0;
            VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
            FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
            return true;
        }
    }
    return false;
}

void CancelRecovery(bool releaseOurClip) {
    if (g_window) KillTimer(g_window, kRecoveryTimer);
    g_state.recovering = false;
    if (releaseOurClip && g_state.clipAppliedByUs.exchange(false) && g_clipCursor) {
        g_clipCursor(nullptr);
    }
}

void BeginFocusLoss() {
    if (!g_state.recovering) {
        g_state.restoreExpected = g_state.clipObserved && g_state.clipActive;
        g_state.focusReturned = false;
        g_state.displayWidth = static_cast<UINT>(GetSystemMetrics(SM_CXSCREEN));
        g_state.displayHeight = static_cast<UINT>(GetSystemMetrics(SM_CYSCREEN));
    }
    CancelRecovery(true);
    g_state.recovering = true;
}

void ArmRecovery(bool displayConfirmed) {
    if (!g_state.recovering || !g_state.restoreExpected || !g_window) return;
    const UINT delay = displayConfirmed || !g_settings.waitForDisplayChange
        ? g_settings.delayMs : 2000 + g_settings.delayMs;
    KillTimer(g_window, kRecoveryTimer);
    SetTimer(g_window, kRecoveryTimer, delay, nullptr);
}

void FocusReturned() {
    if (!g_state.recovering) return;
    g_state.focusReturned = true;
    ArmRecovery(false);
}

void ApplyRecovery() {
    if (!g_state.recovering || !g_state.restoreExpected || !g_clipCursor) return;
    if (GetForegroundWindow() != g_window || GetFocus() != g_window ||
        IsIconic(g_window) || !IsWindowVisible(g_window)) return;

    g_state.recovering = false;

    const RECT expected = ExpectedClip();
    RECT before{};
    if (!GetClipCursor(&before)) return;
    if (RectsEqual(before, expected)) return;

    const BOOL result = g_clipCursor(&expected);
    RECT after{};
    g_state.clipAppliedByUs = result && GetClipCursor(&after) && RectsEqual(after, expected);
}

LRESULT CALLBACK RecoveryWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) BeginFocusLoss();
            else FocusReturned();
            break;
        case WM_ACTIVATEAPP:
            if (!wParam) BeginFocusLoss();
            else FocusReturned();
            break;
        case WM_SETFOCUS:
            FocusReturned();
            break;
        case WM_KILLFOCUS:
            BeginFocusLoss();
            break;
        case WM_DISPLAYCHANGE:
            if (g_state.recovering && g_state.focusReturned &&
                LOWORD(lParam) == g_state.displayWidth &&
                HIWORD(lParam) == g_state.displayHeight) {
                ArmRecovery(true);
            }
            break;
        case WM_TIMER:
            if (wParam == kRecoveryTimer) {
                KillTimer(window, kRecoveryTimer);
                ApplyRecovery();
            }
            break;
        case WM_DESTROY:
        case WM_NCDESTROY:
            CancelRecovery(true);
            break;
    }
    return CallWindowProcW(g_originalWndProc, window, message, wParam, lParam);
}

BOOL CALLBACK FindGameWindow(HWND window, LPARAM output) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == GetCurrentProcessId() && IsWindowVisible(window) &&
        GetWindow(window, GW_OWNER) == nullptr) {
        *reinterpret_cast<HWND*>(output) = window;
        return FALSE;
    }
    return TRUE;
}

}  // namespace

bool Install(const Settings& settings, HANDLE stopEvent) {
    if (!settings.enabled) return true;
    if (!stopEvent || WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) return false;
    g_settings = settings;
    if (!PatchClipCursorImport()) return false;

    for (unsigned attempt = 0; attempt < 300 && !g_window; ++attempt) {
        EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&g_window));
        if (!g_window && WaitForSingleObject(stopEvent, 100) == WAIT_OBJECT_0) {
            Remove();
            return false;
        }
    }
    if (!g_window) {
        Remove();
        return false;
    }

    if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) {
        Remove();
        return false;
    }

    SetLastError(0);
    g_originalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RecoveryWndProc)));
    if (!g_originalWndProc) {
        Remove();
        return false;
    }
    return true;
}

void Remove() {
    CancelRecovery(true);
    if (g_window && g_originalWndProc) {
        SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_originalWndProc));
        g_originalWndProc = nullptr;
    }
    if (g_clipCursorSlot && *g_clipCursorSlot == reinterpret_cast<void*>(HookClipCursor)) {
        DWORD oldProtect = 0;
        if (VirtualProtect(g_clipCursorSlot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
            *g_clipCursorSlot = reinterpret_cast<void*>(g_clipCursor);
            DWORD ignored = 0;
            VirtualProtect(g_clipCursorSlot, sizeof(void*), oldProtect, &ignored);
        }
    }
    g_clipCursorSlot = nullptr;
    g_clipCursor = nullptr;
    g_window = nullptr;
}

}  // namespace cursor_clip_recovery
