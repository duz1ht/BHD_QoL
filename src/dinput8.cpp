#include <windows.h>
#include <shlwapi.h>

#include <cstddef>
#include <cstdint>

#include "logger.h"
#include "dpi_awareness.h"
#include "borderless_fullscreen.h"
#include "borderless_gamma.h"
#include "game_window.h"
#include "raw_input.h"
#include "mouse_scaling_fix.h"
#include "camera_fov.h"

namespace {
constexpr uintptr_t kImageBase = 0x400000;
constexpr uintptr_t kPatchAddressMovEax = 0x52A934;
constexpr uintptr_t kPatchAddressWidthStore = 0x52A94C;
constexpr uintptr_t kPatchAddressHeightStore = 0x52A956;
constexpr uintptr_t kPatchAddressLoadingGameDynamicResolution = 0x4BC711;
constexpr uintptr_t kPatchAddressInitializeInGameSystemsClipCursor = 0x4623D2;
constexpr uintptr_t kPatchAddressClipCursorToViewPort = 0x4628D3;
constexpr uintptr_t kDynamicResolutionCodeCave = 0x5E4879;
constexpr uintptr_t kInitialClipCursorCodeCave = 0x5E4912;
constexpr uintptr_t kClipCursorCodeCave = 0x5E492B;

HMODULE g_realDInput8 = nullptr;
volatile LONG g_initialized = 0;
struct PatchConfig {
    bool nvgResolution = true;
    bool dynamicResolution = true;
    bool clipCursorFix = true;
    bool rawMouseInput = true;
    bool restoreCursorClip = true;
    bool mouseScalingFix = true;
    bool useCorrectAspectFov = true;
    bool dpiAware = true;
    bool borderlessFullscreen = false;
    bool borderlessGamma = true;
    bool forceDesktopResolution = false;
    bool loggingEnabled = false;
    unsigned long rawInputStatisticsIntervalMs = 5000;
    bool invalidStatisticsInterval = false;
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
    0x89, 0x1D, 0x30, 0xC3, 0x60, 0x00, 0xE9, 0x57, 0x7E, 0xED, 0xFF,
};

const unsigned char kInitialClipCursorCodeCaveBytes[] = {
    0x8B, 0x05, 0xC0,
    0x72, 0x9F, 0x00, 0x48, 0x89, 0x45, 0xF8, 0x8B, 0x05, 0xC4, 0x72, 0x9F,
    0x00, 0x48, 0x89, 0x45, 0xFC, 0xE9, 0xAC, 0xDA, 0xE7, 0xFF,
};

const unsigned char kClipCursorCodeCaveBytes[] = {
    0x8B, 0x05, 0xC0, 0x72, 0x9F, 0x00, 0x48, 0x89, 0x45, 0xF8, 0x8B, 0x05,
    0xC4, 0x72, 0x9F, 0x00, 0x48, 0x89, 0x45, 0xFC, 0xE9, 0x9D, 0xDF, 0xE7,
    0xFF,
};

const unsigned char kLoadingDynamicResolutionExpected[] = {0x89, 0x1D, 0xD8, 0xB3, 0x9F, 0x00};
const unsigned char kLoadingDynamicResolutionReplacement[] = {0xE9, 0x63, 0x81, 0x12, 0x00, 0x90};
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
    "Writes dynamic-resolution calculations before redirecting fixed-resolution instructions.",
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

const Patch kInitialClipCursorCodeCavePatch = {
    "Install initial ClipCursor code cave",
    "Writes dynamic ClipCursor bounds used by InitializeInGameSystems().",
    kInitialClipCursorCodeCave,
    {nullptr, sizeof(kInitialClipCursorCodeCaveBytes)},
    BYTE_SPAN(kInitialClipCursorCodeCaveBytes),
    true,
};

const Patch kClipCursorCodeCavePatch = {
    "Install viewport ClipCursor code cave",
    "Writes dynamic ClipCursor bounds used by ClipCursorToViewPort().",
    kClipCursorCodeCave,
    {nullptr, sizeof(kClipCursorCodeCaveBytes)},
    BYTE_SPAN(kClipCursorCodeCaveBytes),
    true,
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
        logger::Log("INFO", "Patch", "address=0x%08lX status=already_applied title=%s",
                    static_cast<unsigned long>(patch.virtualAddress), patch.title);
        return true;
    }

    const bool expectedMatches = BytesEqual(address, patch.expected);
    const bool zeroFilledMatches = patch.allowZeroFilledExpected && BytesAreZeroFilled(address, patch.expected.size);
    if (!expectedMatches && !zeroFilledMatches) {
        logger::Log("ERROR", "Patch", "address=0x%08lX status=unexpected_bytes title=%s",
                    static_cast<unsigned long>(patch.virtualAddress), patch.title);
        return false;
    }

    if (!WriteBytes(patch.virtualAddress, patch.replacement)) {
        logger::Log("ERROR", "Patch", "address=0x%08lX status=write_failed error=%lu title=%s",
                    static_cast<unsigned long>(patch.virtualAddress), GetLastError(), patch.title);
        return false;
    }

    logger::Log("INFO", "Patch", "address=0x%08lX status=applied title=%s description=%s",
                static_cast<unsigned long>(patch.virtualAddress), patch.title, patch.description);
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
    config.clipCursorFix = BoolFromIni(iniPath, L"ClipCursorFix", config.clipCursorFix);
    config.rawMouseInput = BoolFromIni(iniPath, L"RawMouseInput", config.rawMouseInput);
    config.restoreCursorClip =
        BoolFromIni(iniPath, L"RestoreCursorClip", config.restoreCursorClip);
    config.mouseScalingFix = BoolFromIni(iniPath, L"MouseScalingFix", config.mouseScalingFix);
    config.useCorrectAspectFov =
        BoolFromIni(iniPath, L"UseCorrectAspectFOV", config.useCorrectAspectFov);
    config.dpiAware = BoolFromIni(iniPath, L"DPIAware", config.dpiAware);
    config.borderlessFullscreen =
        BoolFromIni(iniPath, L"BorderlessFullscreen", config.borderlessFullscreen);
    config.borderlessGamma = BoolFromIni(iniPath, L"BorderlessGamma", config.borderlessGamma);
    config.forceDesktopResolution =
        BoolFromIni(iniPath, L"ForceDesktopResolution", config.forceDesktopResolution);
    config.loggingEnabled =
        GetPrivateProfileIntW(L"Logging", L"Enabled", config.loggingEnabled ? 1 : 0, iniPath) != 0;
    const int interval = GetPrivateProfileIntW(L"Logging", L"RawInputStatisticsIntervalMs",
                                                config.rawInputStatisticsIntervalMs, iniPath);
    config.invalidStatisticsInterval = interval < 1000 || interval > 60000;
    config.rawInputStatisticsIntervalMs = config.invalidStatisticsInterval
                                              ? 5000
                                              : static_cast<unsigned long>(interval);
    return config;
}

void ApplyBhdPatches() {
    const PatchConfig config = LoadPatchConfig();
    logger::Initialize(config.loggingEnabled);
    logger::Log("INFO", "Config",
                "NVGResolution=%d DynamicResolution=%d ClipCursorFix=%d RawMouseInput=%d "
                "RestoreCursorClip=%d MouseScalingFix=%d UseCorrectAspectFOV=%d "
                "DPIAware=%d BorderlessFullscreen=%d BorderlessGamma=%d ForceDesktopResolution=%d "
                "RawInputStatisticsIntervalMs=%lu",
                config.nvgResolution, config.dynamicResolution, config.clipCursorFix,
                config.rawMouseInput, config.restoreCursorClip, config.mouseScalingFix,
                config.useCorrectAspectFov, config.dpiAware,
                config.borderlessFullscreen, config.borderlessGamma, config.forceDesktopResolution,
                config.rawInputStatisticsIntervalMs);
    if (config.invalidStatisticsInterval) {
        logger::Log("WARN", "Config",
                    "invalid RawInputStatisticsIntervalMs; using default 5000");
    }
    const uintptr_t imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (imageBase != kImageBase) {
        logger::Log("ERROR", "Executable", "unsupported image base: expected=0x%08lX actual=0x%08lX",
                    static_cast<unsigned long>(kImageBase), static_cast<unsigned long>(imageBase));
        return;
    }
    logger::Log("INFO", "Executable", "image base validated: 0x%08lX",
                static_cast<unsigned long>(imageBase));
    mouse_scaling_fix::Install(config.mouseScalingFix);
    camera_fov::Install(config.useCorrectAspectFov);
    dpi_awareness::Initialize(config.dpiAware);
    if (config.borderlessFullscreen && !config.dpiAware) {
        logger::Log("WARN", "BorderlessFullscreen",
                    "DPIAware=0 may virtualize monitor coordinates and dimensions");
    }
    const bool borderlessInitialized =
        borderless_fullscreen::Initialize(config.borderlessFullscreen,
                                          config.forceDesktopResolution);
    const bool gammaInitialized = borderless_gamma::Initialize(
        config.borderlessFullscreen && config.borderlessGamma && borderlessInitialized);

    if (config.nvgResolution) {
        logger::Log("INFO", "NVGResolution", "feature enabled");
        ApplyPatchGroup(kNvgPatches);
    } else {
        logger::Log("INFO", "NVGResolution", "feature disabled");
    }

    if (config.dynamicResolution) {
        logger::Log("INFO", "DynamicResolution", "feature enabled");
        if (ApplyPatch(kDynamicResolutionCodeCavePatch)) {
            ApplyPatch(kDynamicResolutionCorePatch);
        }
    } else {
        logger::Log("INFO", "DynamicResolution", "feature disabled");
    }
    if (config.clipCursorFix) {
        logger::Log("INFO", "ClipCursorFix", "feature enabled");
        if (ApplyPatch(kInitialClipCursorCodeCavePatch) && ApplyPatch(kClipCursorCodeCavePatch)) {
            ApplyPatchGroup(kClipCursorPatches);
        }
    } else {
        logger::Log("INFO", "ClipCursorFix", "feature disabled");
    }
    const bool rawInstalled =
        raw_input::Install({config.rawMouseInput, config.rawInputStatisticsIntervalMs});
    game_window::Configure(
        {config.rawMouseInput && rawInstalled, config.restoreCursorClip,
         config.borderlessFullscreen && borderlessInitialized,
         config.borderlessFullscreen && config.borderlessGamma && gammaInitialized,
         config.mouseScalingFix});
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
    if (g_realDInput8 == nullptr) {
        logger::Log("ERROR", "Proxy", "LoadLibraryW failed for system dinput8.dll: error=%lu",
                    GetLastError());
    } else {
        logger::Log("INFO", "Proxy", "system dinput8.dll loaded at 0x%08lX",
                    reinterpret_cast<unsigned long>(g_realDInput8));
    }
    return g_realDInput8;
}

void EnsureInitialized() {
    if (InterlockedCompareExchange(&g_initialized, 1, 0) == 0) {
        ApplyBhdPatches();
        LoadRealDInput8();
        InterlockedExchange(&g_initialized, 2);
        return;
    }
    while (InterlockedCompareExchange(&g_initialized, 2, 2) != 2) Sleep(0);
}

template <typename Function>
Function GetRealProc(const char* name) {
    HMODULE realDll = LoadRealDInput8();
    if (realDll == nullptr) {
        return nullptr;
    }
    const auto function = reinterpret_cast<Function>(GetProcAddress(realDll, name));
    if (function == nullptr) {
        logger::Log("ERROR", "Proxy", "GetProcAddress(%s) failed: error=%lu", name, GetLastError());
    }
    return function;
}
}  // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD version, REFIID riidltf,
                                              LPVOID* out, LPUNKNOWN outer) {
    EnsureInitialized();
    using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    const auto real = GetRealProc<DirectInput8CreateFn>("DirectInput8Create");
    if (real == nullptr) {
        return E_FAIL;
    }
    return real(hinst, version, riidltf, out, outer);
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
    EnsureInitialized();
    using Fn = HRESULT(WINAPI*)();
    const auto real = GetRealProc<Fn>("DllCanUnloadNow");
    return real != nullptr ? real() : S_FALSE;
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* out) {
    EnsureInitialized();
    using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    const auto real = GetRealProc<Fn>("DllGetClassObject");
    return real != nullptr ? real(rclsid, riid, out) : E_FAIL;
}

extern "C" HRESULT WINAPI DllRegisterServer() {
    EnsureInitialized();
    using Fn = HRESULT(WINAPI*)();
    const auto real = GetRealProc<Fn>("DllRegisterServer");
    return real != nullptr ? real() : E_FAIL;
}

extern "C" HRESULT WINAPI DllUnregisterServer() {
    EnsureInitialized();
    using Fn = HRESULT(WINAPI*)();
    const auto real = GetRealProc<Fn>("DllUnregisterServer");
    return real != nullptr ? real() : E_FAIL;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
