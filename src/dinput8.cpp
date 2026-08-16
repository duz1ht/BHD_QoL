#include <windows.h>
#include <shlwapi.h>

#include <cstddef>
#include <cstdint>

namespace {
constexpr uintptr_t kImageBase = 0x400000;
constexpr uintptr_t kPatchAddressMovEax = 0x52A934;
constexpr uintptr_t kPatchAddressWidthStore = 0x52A94C;
constexpr uintptr_t kPatchAddressHeightStore = 0x52A956;
constexpr uintptr_t kPatchAddressLoadingGameDynamicResolution = 0x4BC711;
constexpr uintptr_t kPatchAddressPollMouseInputCursorPosition = 0x567972;
constexpr uintptr_t kPatchAddressInitializeInGameSystemsClipCursor = 0x4623D2;
constexpr uintptr_t kPatchAddressClipCursorToViewPort = 0x4628D3;
constexpr uintptr_t kDynamicResolutionCodeCave = 0x5E4879;
constexpr UINT_PTR kCursorClipRecoveryTimer = 0x42484451;
constexpr UINT kCursorClipRecoveryDelayMs = 100;

HMODULE g_realDInput8 = nullptr;
bool g_logAppliedPatches = false;
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
ClipCursorFn g_originalClipCursor = nullptr;
RECT g_lastCursorClip = {};
bool g_hasLastCursorClip = false;
RECT g_cursorClipVirtualScreen = {};
RECT g_cursorClipClientRect = {};
RECT g_cursorClipMonitorRect = {};
HMONITOR g_cursorClipMonitor = nullptr;
HWND g_cursorClipWindow = nullptr;
bool g_hasCursorClipContext = false;
bool g_cursorClipDisplayChanged = false;
HWND g_gameWindow = nullptr;
WNDPROC g_originalGameWindowProc = nullptr;

struct PatchConfig {
    bool nvgResolution = true;
    bool dynamicResolution = true;
    bool mouseCursorFix = true;
    bool clipCursorFix = true;
    bool cursorClipRecovery = true;
    bool logAppliedPatches = false;
};

struct ByteSpan {
    const unsigned char* bytes;
    size_t size;
};

struct Patch {
    const char* title;
    const char* description;
    uintptr_t virtualAddress;
    ByteSpan expected;
    ByteSpan replacement;
    bool allowZeroFilledExpected;
};

#define BYTE_SPAN(name) ByteSpan{name, sizeof(name)}

const unsigned char kNvgWidthImmediateExpected[] = {0xB8, 0x00, 0x02, 0x00, 0x00};
const unsigned char kNvgWidthImmediateReplacement[] = {0xB8, 0x00, 0x08, 0x00, 0x00};
const unsigned char kNvgStoredWidthExpected[] = {0xC7, 0x05, 0x8C, 0x49, 0xE0, 0x00, 0x00, 0x02, 0x00, 0x00};
const unsigned char kNvgStoredWidthReplacement[] = {0xC7, 0x05, 0x8C, 0x49, 0xE0, 0x00, 0x00, 0x08, 0x00, 0x00};
const unsigned char kNvgStoredHeightExpected[] = {0xC7, 0x05, 0x90, 0x49, 0xE0, 0x00, 0x00, 0x01, 0x00, 0x00};
const unsigned char kNvgStoredHeightReplacement[] = {0xC7, 0x05, 0x90, 0x49, 0xE0, 0x00, 0x00, 0x04, 0x00, 0x00};

const unsigned char kDynamicResolutionCodeCaveBytes[] = {
    0x89, 0x1D, 0xD8, 0xB3, 0x9F, 0x00, 0x8B, 0x1D, 0xC0, 0x72, 0x9F, 0x00,
    0xD1, 0xFB, 0x89, 0x1D, 0x22, 0xC3, 0x60, 0x00, 0x8B, 0x1D, 0xC4, 0x72,
    0x9F, 0x00, 0xD1, 0xFB, 0x89, 0x1D, 0x26, 0xC3, 0x60, 0x00, 0x8B, 0x1D,
    0xC0, 0x72, 0x9F, 0x00, 0xD1, 0xFB, 0xF7, 0xDB, 0x89, 0x1D, 0x2B, 0xC3,
    0x60, 0x00, 0x8B, 0x1D, 0xC4, 0x72, 0x9F, 0x00, 0xD1, 0xFB, 0xF7, 0xDB,
    0x89, 0x1D, 0x30, 0xC3, 0x60, 0x00, 0xE9, 0x57, 0x7E, 0xED, 0xFF, 0x03,
    0x05, 0x26, 0xC3, 0x60, 0x00, 0x50, 0x03, 0x0D, 0x22, 0xC3, 0x60, 0x00,
    0x51, 0xFF, 0x15, 0xD8, 0x12, 0x61, 0x00, 0x8B, 0x15, 0xE0, 0x55, 0xF6,
    0x00, 0x8B, 0x05, 0xE4, 0x55, 0xF6, 0x00, 0x03, 0x15, 0x2B, 0xC3, 0x60,
    0x00, 0x03, 0x05, 0x30, 0xC3, 0x60, 0x00, 0x89, 0x15, 0xEC, 0x55, 0xF6,
    0x00, 0xA3, 0xF0, 0x55, 0xF6, 0x00, 0x8B, 0x05, 0x22, 0xC3, 0x60, 0x00,
    0xA3, 0xE0, 0x55, 0xF6, 0x00, 0x8B, 0x05, 0x26, 0xC3, 0x60, 0x00, 0xA3,
    0xE4, 0x55, 0xF6, 0x00, 0xE9, 0xA8, 0x30, 0xF8, 0xFF, 0x8B, 0x05, 0xC0,
    0x72, 0x9F, 0x00, 0x48, 0x89, 0x45, 0xF8, 0x8B, 0x05, 0xC4, 0x72, 0x9F,
    0x00, 0x48, 0x89, 0x45, 0xFC, 0xE9, 0xAC, 0xDA, 0xE7, 0xFF, 0x8B, 0x05,
    0xC0, 0x72, 0x9F, 0x00, 0x48, 0x89, 0x45, 0xF8, 0x8B, 0x05, 0xC4, 0x72,
    0x9F, 0x00, 0x48, 0x89, 0x45, 0xFC, 0xE9, 0x9D, 0xDF, 0xE7, 0xFF,
};

const unsigned char kLoadingDynamicResolutionExpected[] = {0x89, 0x1D, 0xD8, 0xB3, 0x9F, 0x00};
const unsigned char kLoadingDynamicResolutionReplacement[] = {0xE9, 0x63, 0x81, 0x12, 0x00, 0x90};
const unsigned char kMouseCursorExpected[] = {0x05, 0xF0, 0x00, 0x00, 0x00};
const unsigned char kMouseCursorReplacement[] = {0xE9, 0x49, 0xCF, 0x07, 0x00};
const unsigned char kInitialClipCursorExpected[] = {0xC7, 0x45, 0xF8, 0x7F, 0x02, 0x00, 0x00};
const unsigned char kInitialClipCursorReplacement[] = {0xE9, 0x3B, 0x25, 0x18, 0x00, 0x90, 0x90};
const unsigned char kClipCursorExpected[] = {0xC7, 0x45, 0xF8, 0x7F, 0x02, 0x00, 0x00};
const unsigned char kClipCursorReplacement[] = {0xE9, 0x53, 0x20, 0x18, 0x00, 0x90, 0x90};

const Patch kNvgPatches[] = {
    {"Increase NVG viewport width immediate", "SetupNVGViewPort(): MOV EAX,0x200 -> MOV EAX,0x800",
     kPatchAddressMovEax, BYTE_SPAN(kNvgWidthImmediateExpected), BYTE_SPAN(kNvgWidthImmediateReplacement), false},
    {"Increase NVG viewport stored width", "SetupNVGViewPort(): MOV [00E0498C],0x200 -> MOV [00E0498C],0x800",
     kPatchAddressWidthStore, BYTE_SPAN(kNvgStoredWidthExpected), BYTE_SPAN(kNvgStoredWidthReplacement), false},
    {"Increase NVG viewport stored height", "SetupNVGViewPort(): MOV [00E04990],0x100 -> MOV [00E04990],0x400",
     kPatchAddressHeightStore, BYTE_SPAN(kNvgStoredHeightExpected), BYTE_SPAN(kNvgStoredHeightReplacement), false},
};

const Patch kDynamicResolutionCodeCavePatch = {
    "Install dynamic resolution code cave",
    "Writes resolution, mouse, and ClipCursor logic before redirecting fixed-resolution instructions.",
    kDynamicResolutionCodeCave,
    {nullptr, sizeof(kDynamicResolutionCodeCaveBytes)},
    BYTE_SPAN(kDynamicResolutionCodeCaveBytes),
    true,
};

const Patch kDynamicResolutionCorePatch = {
    "Compute additional dynamic resolution values",
    "LoadingGame(): MOV DWORD PTR [009FB3D8],EBX -> JMP 005E4879",
    kPatchAddressLoadingGameDynamicResolution,
    BYTE_SPAN(kLoadingDynamicResolutionExpected),
    BYTE_SPAN(kLoadingDynamicResolutionReplacement),
    false,
};

const Patch kMouseCursorPatch = {
    "Use dynamic resolution in SetCursorPosition",
    "PollMouseInput(): ADD EAX,0xF0 -> JMP 005E48C0",
    kPatchAddressPollMouseInputCursorPosition,
    BYTE_SPAN(kMouseCursorExpected),
    BYTE_SPAN(kMouseCursorReplacement),
    false,
};

const Patch kClipCursorPatches[] = {
    {"Use dynamic resolution in initial ClipCursor setup",
     "InitializeInGameSystems(): MOV DWORD PTR [EBP + -0x8],0x27F -> JMP 005E4912",
     kPatchAddressInitializeInGameSystemsClipCursor, BYTE_SPAN(kInitialClipCursorExpected),
     BYTE_SPAN(kInitialClipCursorReplacement), false},
    {"Use dynamic resolution when clipping cursor to viewport",
     "ClipCursorToViewPort(): MOV DWORD PTR [EBP + -0x8],0x27F -> JMP 005E492B",
     kPatchAddressClipCursorToViewPort, BYTE_SPAN(kClipCursorExpected), BYTE_SPAN(kClipCursorReplacement), false},
};

bool BytesEqual(const unsigned char* current, ByteSpan expected) {
    if (expected.bytes == nullptr) {
        return false;
    }
    for (size_t i = 0; i < expected.size; ++i) {
        if (current[i] != expected.bytes[i]) {
            return false;
        }
    }
    return true;
}

bool BytesAreZeroFilled(const unsigned char* current, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (current[i] != 0x00) {
            return false;
        }
    }
    return true;
}

void LogPatchMessage(const char* title, const char* status) {
    if (!g_logAppliedPatches) {
        return;
    }

    char message[512] = {};
    wsprintfA(message, "BHD_QoL: %s: %s\n", status, title);
    OutputDebugStringA(message);
}

bool IsValidCursorClip(const RECT* rect) {
    return rect != nullptr && rect->right > rect->left && rect->bottom > rect->top;
}

RECT GetVirtualScreenRect() {
    const LONG left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const LONG top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return {left, top, left + GetSystemMetrics(SM_CXVIRTUALSCREEN),
            top + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}

bool GetWindowClientScreenRect(HWND window, RECT* rect) {
    if (rect == nullptr || !GetClientRect(window, rect)) {
        return false;
    }

    POINT points[] = {{rect->left, rect->top}, {rect->right, rect->bottom}};
    SetLastError(ERROR_SUCCESS);
    if (MapWindowPoints(window, nullptr, points, 2) == 0 && GetLastError() != ERROR_SUCCESS) {
        return false;
    }

    *rect = {points[0].x, points[0].y, points[1].x, points[1].y};
    return IsValidCursorClip(rect);
}

bool RectsEqual(const RECT& left, const RECT& right) {
    return left.left == right.left && left.top == right.top && left.right == right.right &&
           left.bottom == right.bottom;
}

bool GetMonitorRect(HMONITOR monitor, RECT* rect) {
    if (monitor == nullptr || rect == nullptr) {
        return false;
    }

    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoA(monitor, &info)) {
        return false;
    }

    *rect = info.rcMonitor;
    return IsValidCursorClip(rect);
}

bool IsCursorClipSafeToRestore(HWND gameWindow) {
    const RECT virtualScreen = GetVirtualScreenRect();
    RECT clientRect = {};
    RECT screenIntersection = {};
    RECT clientIntersection = {};
    if (!IsValidCursorClip(&virtualScreen) || !GetWindowClientScreenRect(gameWindow, &clientRect) ||
        !IntersectRect(&screenIntersection, &g_lastCursorClip, &virtualScreen) ||
        !EqualRect(&screenIntersection, &g_lastCursorClip) ||
        !IntersectRect(&clientIntersection, &g_lastCursorClip, &clientRect)) {
        return false;
    }

    const LONG clipWidth = clientIntersection.right - clientIntersection.left;
    const LONG clipHeight = clientIntersection.bottom - clientIntersection.top;
    const LONG clientWidth = clientRect.right - clientRect.left;
    const LONG clientHeight = clientRect.bottom - clientRect.top;
    if (clipWidth < clientWidth / 2 || clipHeight < clientHeight / 2) {
        return false;
    }

    if (g_cursorClipDisplayChanged) {
        const HMONITOR currentMonitor = MonitorFromRect(&g_lastCursorClip, MONITOR_DEFAULTTONULL);
        RECT currentMonitorRect = {};
        if (!g_hasCursorClipContext || !RectsEqual(virtualScreen, g_cursorClipVirtualScreen) ||
            !RectsEqual(clientRect, g_cursorClipClientRect) || currentMonitor != g_cursorClipMonitor ||
            !GetMonitorRect(currentMonitor, &currentMonitorRect) ||
            !RectsEqual(currentMonitorRect, g_cursorClipMonitorRect)) {
            return false;
        }
    }

    return true;
}

void ClearSavedCursorClip(const char* reason) {
    g_hasLastCursorClip = false;
    g_cursorClipWindow = nullptr;
    g_cursorClipMonitor = nullptr;
    g_hasCursorClipContext = false;
    g_cursorClipDisplayChanged = false;
    LogPatchMessage("Saved cursor clip", reason);
}

void RestoreCursorClip(HWND gameWindow) {
    if (g_originalClipCursor != nullptr && g_hasLastCursorClip && gameWindow == g_gameWindow &&
        gameWindow == g_cursorClipWindow &&
        GetForegroundWindow() == gameWindow && GetFocus() == gameWindow && !IsIconic(gameWindow) &&
        IsWindowVisible(gameWindow) && IsCursorClipSafeToRestore(gameWindow)) {
        g_originalClipCursor(&g_lastCursorClip);
        g_cursorClipDisplayChanged = false;
        LogPatchMessage("Restore saved cursor clip", "applied");
    } else if (g_hasLastCursorClip && gameWindow == g_gameWindow &&
               gameWindow == g_cursorClipWindow && GetForegroundWindow() == gameWindow &&
               GetFocus() == gameWindow && !IsIconic(gameWindow) && IsWindowVisible(gameWindow)) {
        ClearSavedCursorClip("discarded stale or unsafe rectangle");
    }
}

LRESULT CALLBACK CursorClipRecoveryWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    const WNDPROC originalWindowProc = g_originalGameWindowProc;

    if (message == WM_DISPLAYCHANGE) {
        g_cursorClipDisplayChanged = true;
        SetTimer(window, kCursorClipRecoveryTimer, kCursorClipRecoveryDelayMs, nullptr);
    } else if ((message == WM_ACTIVATEAPP && wParam != FALSE) || message == WM_SETFOCUS) {
        SetTimer(window, kCursorClipRecoveryTimer, kCursorClipRecoveryDelayMs, nullptr);
    } else if (message == WM_TIMER && wParam == kCursorClipRecoveryTimer) {
        KillTimer(window, kCursorClipRecoveryTimer);
        RestoreCursorClip(window);
    } else if (message == WM_NCDESTROY && window == g_gameWindow) {
        KillTimer(window, kCursorClipRecoveryTimer);
        SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWindowProc));
        g_gameWindow = nullptr;
        g_originalGameWindowProc = nullptr;
    }

    return originalWindowProc != nullptr
               ? CallWindowProcA(originalWindowProc, window, message, wParam, lParam)
               : DefWindowProcA(window, message, wParam, lParam);
}

void AttachCursorClipRecoveryToWindow() {
    HWND window = GetFocus();
    if (window == nullptr) {
        window = GetForegroundWindow();
    }
    if (window == nullptr || g_gameWindow != nullptr ||
        GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) {
        return;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous =
        SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CursorClipRecoveryWindowProc));
    if (previous == 0 && GetLastError() != ERROR_SUCCESS) {
        LogPatchMessage("Install cursor clip recovery window hook", "failed SetWindowLongPtr");
        return;
    }

    g_gameWindow = window;
    g_originalGameWindowProc = reinterpret_cast<WNDPROC>(previous);
    LogPatchMessage("Install cursor clip recovery window hook", "applied");
}

BOOL WINAPI HookClipCursor(const RECT* rect) {
    const BOOL result = g_originalClipCursor != nullptr ? g_originalClipCursor(rect) : FALSE;

    if (rect == nullptr) {
        ClearSavedCursorClip("released by game");
    } else if (result != FALSE && IsValidCursorClip(rect)) {
        AttachCursorClipRecoveryToWindow();
        g_lastCursorClip = *rect;
        g_hasLastCursorClip = true;
        g_cursorClipVirtualScreen = GetVirtualScreenRect();
        g_cursorClipMonitor = MonitorFromRect(rect, MONITOR_DEFAULTTONULL);
        g_cursorClipWindow = g_gameWindow;
        g_hasCursorClipContext =
            g_cursorClipWindow != nullptr &&
            GetWindowClientScreenRect(g_cursorClipWindow, &g_cursorClipClientRect) &&
            GetMonitorRect(g_cursorClipMonitor, &g_cursorClipMonitorRect);
        g_cursorClipDisplayChanged = false;
    }

    return result;
}

bool InstallCursorClipRecoveryHook() {
    HMODULE executable = GetModuleHandleW(nullptr);
    auto* base = reinterpret_cast<unsigned char*>(executable);
    auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dosHeader == nullptr || dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    const IMAGE_DATA_DIRECTORY& imports =
        ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imports.VirtualAddress == 0) {
        return false;
    }

    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor) {
        if (descriptor->OriginalFirstThunk == 0 || descriptor->FirstThunk == 0) {
            continue;
        }

        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        auto* functions = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++functions) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
                continue;
            }
            auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (lstrcmpA(reinterpret_cast<const char*>(import->Name), "ClipCursor") != 0) {
                continue;
            }

            auto* slot = reinterpret_cast<ULONG_PTR*>(&functions->u1.Function);
            g_originalClipCursor = reinterpret_cast<ClipCursorFn>(*slot);
            DWORD oldProtect = 0;
            if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
                g_originalClipCursor = nullptr;
                return false;
            }
            *slot = reinterpret_cast<ULONG_PTR>(HookClipCursor);
            DWORD ignored = 0;
            VirtualProtect(slot, sizeof(*slot), oldProtect, &ignored);
            FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
            LogPatchMessage("Install cursor clip recovery API hook", "applied");
            return true;
        }
    }

    LogPatchMessage("Install cursor clip recovery API hook", "ClipCursor import not found");
    return false;
}

bool WriteBytes(uintptr_t virtualAddress, ByteSpan replacement) {
    auto* address = reinterpret_cast<unsigned char*>(virtualAddress);

    DWORD oldProtect = 0;
    if (!VirtualProtect(address, replacement.size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    for (size_t i = 0; i < replacement.size; ++i) {
        address[i] = replacement.bytes[i];
    }
    FlushInstructionCache(GetCurrentProcess(), address, replacement.size);

    DWORD ignored = 0;
    VirtualProtect(address, replacement.size, oldProtect, &ignored);
    return true;
}

bool ApplyPatch(const Patch& patch) {
    auto* address = reinterpret_cast<unsigned char*>(patch.virtualAddress);
    if (BytesEqual(address, patch.replacement)) {
        LogPatchMessage(patch.title, "already applied");
        return true;
    }

    const bool expectedMatches = BytesEqual(address, patch.expected);
    const bool zeroFilledMatches = patch.allowZeroFilledExpected && BytesAreZeroFilled(address, patch.expected.size);
    if (!expectedMatches && !zeroFilledMatches) {
        LogPatchMessage(patch.title, "skipped unexpected bytes");
        return false;
    }

    if (!WriteBytes(patch.virtualAddress, patch.replacement)) {
        LogPatchMessage(patch.title, "failed VirtualProtect/write");
        return false;
    }

    LogPatchMessage(patch.title, "applied");
    return true;
}

template <size_t Count>
bool ApplyPatchGroup(const Patch (&patches)[Count]) {
    bool appliedAll = true;
    for (const auto& patch : patches) {
        appliedAll = ApplyPatch(patch) && appliedAll;
    }
    return appliedAll;
}

bool BoolFromIni(const wchar_t* path, const wchar_t* key, bool defaultValue) {
    return GetPrivateProfileIntW(L"PatchGroups", key, defaultValue ? 1 : 0, path) != 0;
}

bool DebugBoolFromIni(const wchar_t* path, const wchar_t* key, bool defaultValue) {
    return GetPrivateProfileIntW(L"Debug", key, defaultValue ? 1 : 0, path) != 0;
}

void BuildIniPath(wchar_t* iniPath, DWORD size) {
    iniPath[0] = L'\0';
    const DWORD length = GetModuleFileNameW(nullptr, iniPath, size);
    if (length == 0 || length >= size) {
        lstrcpynW(iniPath, L".\\dinput8.ini", size);
        return;
    }

    PathRemoveFileSpecW(iniPath);
    PathAppendW(iniPath, L"dinput8.ini");
}

PatchConfig LoadPatchConfig() {
    wchar_t iniPath[MAX_PATH] = {};
    BuildIniPath(iniPath, MAX_PATH);

    PatchConfig config = {};
    config.nvgResolution = BoolFromIni(iniPath, L"NVGResolution", config.nvgResolution);
    config.dynamicResolution = BoolFromIni(iniPath, L"DynamicResolution", config.dynamicResolution);
    config.mouseCursorFix = BoolFromIni(iniPath, L"MouseCursorFix", config.mouseCursorFix);
    config.clipCursorFix = BoolFromIni(iniPath, L"ClipCursorFix", config.clipCursorFix);
    config.cursorClipRecovery =
        BoolFromIni(iniPath, L"CursorClipRecovery", config.cursorClipRecovery);
    config.logAppliedPatches = DebugBoolFromIni(iniPath, L"LogAppliedPatches", config.logAppliedPatches);
    return config;
}

void ApplyBhdPatches() {
    if (reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) != kImageBase) {
        return;
    }

    const PatchConfig config = LoadPatchConfig();
    g_logAppliedPatches = config.logAppliedPatches;

    if (config.cursorClipRecovery) {
        InstallCursorClipRecoveryHook();
    }

    if (config.nvgResolution) {
        ApplyPatchGroup(kNvgPatches);
    }

    const bool needsDynamicResolutionCodeCave =
        config.dynamicResolution || config.mouseCursorFix || config.clipCursorFix;
    if (!needsDynamicResolutionCodeCave) {
        return;
    }

    if (!ApplyPatch(kDynamicResolutionCodeCavePatch)) {
        return;
    }

    if (config.dynamicResolution || config.mouseCursorFix) {
        ApplyPatch(kDynamicResolutionCorePatch);
    }
    if (config.mouseCursorFix) {
        ApplyPatch(kMouseCursorPatch);
    }
    if (config.clipCursorFix) {
        ApplyPatchGroup(kClipCursorPatches);
    }
}

HMODULE LoadRealDInput8() {
    if (g_realDInput8 != nullptr) {
        return g_realDInput8;
    }

    wchar_t systemPath[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(systemPath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return nullptr;
    }

    if (!PathAppendW(systemPath, L"dinput8.dll")) {
        return nullptr;
    }

    g_realDInput8 = LoadLibraryW(systemPath);
    return g_realDInput8;
}

template <typename Function>
Function GetRealProc(const char* name) {
    HMODULE realDll = LoadRealDInput8();
    if (realDll == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<Function>(GetProcAddress(realDll, name));
}
}  // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD version, REFIID riidltf,
                                              LPVOID* out, LPUNKNOWN outer) {
    using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    const auto real = GetRealProc<DirectInput8CreateFn>("DirectInput8Create");
    if (real == nullptr) {
        return E_FAIL;
    }
    return real(hinst, version, riidltf, out, outer);
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
    using Fn = HRESULT(WINAPI*)();
    const auto real = GetRealProc<Fn>("DllCanUnloadNow");
    return real != nullptr ? real() : S_FALSE;
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* out) {
    using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    const auto real = GetRealProc<Fn>("DllGetClassObject");
    return real != nullptr ? real(rclsid, riid, out) : E_FAIL;
}

extern "C" HRESULT WINAPI DllRegisterServer() {
    using Fn = HRESULT(WINAPI*)();
    const auto real = GetRealProc<Fn>("DllRegisterServer");
    return real != nullptr ? real() : E_FAIL;
}

extern "C" HRESULT WINAPI DllUnregisterServer() {
    using Fn = HRESULT(WINAPI*)();
    const auto real = GetRealProc<Fn>("DllUnregisterServer");
    return real != nullptr ? real() : E_FAIL;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        ApplyBhdPatches();
        LoadRealDInput8();
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_realDInput8 != nullptr) {
            FreeLibrary(g_realDInput8);
            g_realDInput8 = nullptr;
        }
    }
    return TRUE;
}
