#include <windows.h>
#include <shlwapi.h>

#include <cstddef>
#include <cstdint>

#include "bhd_patches.h"

#include "dpi_awareness.h"
#include "borderless_fullscreen.h"
#include "borderless_gamma.h"
#include "game_window.h"
#include "raw_input.h"
#include "mouse_scaling_fix.h"
#include "mouse_scale_query.h"
#include "camera_fov.h"
#include "high_frequency_viewmodel.h"

namespace {
constexpr uintptr_t kImageBase = 0x400000;
constexpr uintptr_t kPatchAddressLoadingGameDynamicResolution = 0x4BC711;
constexpr uintptr_t kPatchAddressInitializeInGameSystemsClipCursor = 0x4623D2;
constexpr uintptr_t kPatchAddressClipCursorToViewPort = 0x4628D3;
constexpr uintptr_t kDynamicResolutionCodeCave = 0x5E4879;
constexpr uintptr_t kInitialClipCursorCodeCave = 0x5E4912;
constexpr uintptr_t kClipCursorCodeCave = 0x5E492B;

volatile LONG g_initialized = 0;
struct PatchConfig {
    bool adaptiveScreenCenter = true;
    bool scaleCursorClipToResolution = true;
    bool rawMouseInput = true;
    bool restoreCursorClip = true;
    bool mouseScalingFix = true;
    bool mouseScaleQueryFix = true;
    bool useCorrectAspectFov = true;
    bool highFrequencyCamera = true;
    bool highFrequencyCameraPosition = true;
    bool highFrequencyViewmodel = true;
    bool dpiAware = true;
    bool borderlessFullscreen = true;
    bool forceDesktopResolution = true;
};

struct ByteSpan {
    const unsigned char* bytes;
    size_t size;
};

struct Patch {
    uintptr_t virtualAddress;
    ByteSpan expected;
    ByteSpan replacement;
    bool allowZeroFilledExpected;
};

#define BYTE_SPAN(name) ByteSpan{name, sizeof(name)}

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

const Patch kDynamicResolutionCodeCavePatch = {
    kDynamicResolutionCodeCave,
    {nullptr, sizeof(kDynamicResolutionCodeCaveBytes)},
    BYTE_SPAN(kDynamicResolutionCodeCaveBytes),
    true,
};

const Patch kDynamicResolutionCorePatch = {
    kPatchAddressLoadingGameDynamicResolution,
    BYTE_SPAN(kLoadingDynamicResolutionExpected),
    BYTE_SPAN(kLoadingDynamicResolutionReplacement),
    false,
};

const Patch kInitialClipCursorCodeCavePatch = {
    kInitialClipCursorCodeCave,
    {nullptr, sizeof(kInitialClipCursorCodeCaveBytes)},
    BYTE_SPAN(kInitialClipCursorCodeCaveBytes),
    true,
};

const Patch kClipCursorCodeCavePatch = {
    kClipCursorCodeCave,
    {nullptr, sizeof(kClipCursorCodeCaveBytes)},
    BYTE_SPAN(kClipCursorCodeCaveBytes),
    true,
};

const Patch kClipCursorPatches[] = {
    {kPatchAddressInitializeInGameSystemsClipCursor, BYTE_SPAN(kInitialClipCursorExpected),
     BYTE_SPAN(kInitialClipCursorReplacement), false},
    {kPatchAddressClipCursorToViewPort, BYTE_SPAN(kClipCursorExpected),
     BYTE_SPAN(kClipCursorReplacement), false},
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
        return true;
    }

    const bool expectedMatches = BytesEqual(address, patch.expected);
    const bool zeroFilledMatches = patch.allowZeroFilledExpected && BytesAreZeroFilled(address, patch.expected.size);
    if (!expectedMatches && !zeroFilledMatches) {
        return false;
    }

    return WriteBytes(patch.virtualAddress, patch.replacement);
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
        lstrcpynW(iniPath, L".\\winmm.ini", size);
        return;
    }

    PathRemoveFileSpecW(iniPath);
    PathAppendW(iniPath, L"winmm.ini");
}

PatchConfig LoadPatchConfig() {
    wchar_t iniPath[MAX_PATH] = {};
    BuildIniPath(iniPath, MAX_PATH);

    PatchConfig config = {};
    config.adaptiveScreenCenter =
        BoolFromIni(iniPath, L"AdaptiveScreenCenter", config.adaptiveScreenCenter);
    config.scaleCursorClipToResolution = BoolFromIni(
        iniPath, L"ScaleCursorClipToResolution", config.scaleCursorClipToResolution);
    config.rawMouseInput = BoolFromIni(iniPath, L"RawMouseInput", config.rawMouseInput);
    config.restoreCursorClip =
        BoolFromIni(iniPath, L"RestoreCursorClip", config.restoreCursorClip);
    config.mouseScalingFix = BoolFromIni(iniPath, L"MouseScalingFix", config.mouseScalingFix);
    config.mouseScaleQueryFix =
        BoolFromIni(iniPath, L"MouseScaleQueryFix", config.mouseScaleQueryFix);
    config.useCorrectAspectFov =
        BoolFromIni(iniPath, L"UseCorrectAspectFOV", config.useCorrectAspectFov);
    config.highFrequencyCamera =
        BoolFromIni(iniPath, L"HighFrequencyCamera", config.highFrequencyCamera);
    config.highFrequencyCameraPosition = BoolFromIni(
        iniPath, L"HighFrequencyCameraPosition", config.highFrequencyCameraPosition);
    config.highFrequencyViewmodel = BoolFromIni(
        iniPath, L"HighFrequencyViewmodel", config.highFrequencyViewmodel);
    config.dpiAware = BoolFromIni(iniPath, L"DPIAware", config.dpiAware);
    config.borderlessFullscreen =
        BoolFromIni(iniPath, L"BorderlessFullscreen", config.borderlessFullscreen);
    config.forceDesktopResolution =
        BoolFromIni(iniPath, L"ForceDesktopResolution", config.forceDesktopResolution);
    return config;
}

void ApplyBhdPatches() {
    const PatchConfig config = LoadPatchConfig();
    const uintptr_t imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (imageBase != kImageBase) {
        return;
    }
    mouse_scaling_fix::Install(config.mouseScalingFix);
    mouse_scale_query::Install(config.mouseScaleQueryFix);
    camera_fov::Install(config.useCorrectAspectFov, config.highFrequencyCamera,
                        config.highFrequencyCameraPosition);
    high_frequency_viewmodel::Install(
        (config.highFrequencyCamera || config.highFrequencyCameraPosition) && config.highFrequencyViewmodel);
    dpi_awareness::Initialize(config.dpiAware);
    const bool borderlessInitialized =
        borderless_fullscreen::Initialize(config.borderlessFullscreen,
                                          config.forceDesktopResolution);
    const bool gammaInitialized = borderless_gamma::Initialize(
        config.borderlessFullscreen && borderlessInitialized);

    if (config.adaptiveScreenCenter) {
        if (ApplyPatch(kDynamicResolutionCodeCavePatch)) {
            ApplyPatch(kDynamicResolutionCorePatch);
        }
    }
    if (config.scaleCursorClipToResolution) {
        if (ApplyPatch(kInitialClipCursorCodeCavePatch) && ApplyPatch(kClipCursorCodeCavePatch)) {
            ApplyPatchGroup(kClipCursorPatches);
        }
    }
    const bool rawInstalled =
        raw_input::Install(config.rawMouseInput);
    game_window::Configure(
        {config.rawMouseInput && rawInstalled, config.restoreCursorClip,
         config.borderlessFullscreen && borderlessInitialized,
         config.borderlessFullscreen && gammaInitialized,
         config.mouseScalingFix});
}

}  // namespace

namespace bhd_patches {
void InitializeForWinmmCall(uintptr_t returnAddress) {
    // These are the two timer calls in the supported executable's startup path.
    // Calls from DLL initialization, timer callbacks and unrelated executables
    // must only forward to WinMM, without installing patches or starting threads.
    if (returnAddress != 0x00569F7A && returnAddress != 0x00569FC5) return;
    const auto* image = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if (reinterpret_cast<uintptr_t>(image) != kImageBase) return;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        dos->e_lfanew > 0x1000) return;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.SizeOfImage < 0x00B65600) return;
    const uintptr_t importAddress = returnAddress == 0x00569F7A ? 0x00611374 : 0x00611370;
    const auto* call = reinterpret_cast<const unsigned char*>(returnAddress - 6);
    if (call[0] != 0xFF || call[1] != 0x15 ||
        *reinterpret_cast<const uint32_t*>(call + 2) != importAddress) return;
    if (InterlockedCompareExchange(&g_initialized, 1, 0) != 0) return;
    ApplyBhdPatches();
    InterlockedExchange(&g_initialized, 2);
}
}  // namespace bhd_patches
