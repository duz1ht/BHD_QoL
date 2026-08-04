#include <windows.h>
#include <shlwapi.h>

#include <array>
#include <cstdint>

namespace {
constexpr uintptr_t kImageBase = 0x400000;
constexpr uintptr_t kPatchAddressMovEax = 0x52A934;
constexpr uintptr_t kPatchAddressWidthStore = 0x52A94C;
constexpr uintptr_t kPatchAddressHeightStore = 0x52A956;

HMODULE g_realDInput8 = nullptr;

struct Patch {
    uintptr_t virtualAddress;
    std::array<unsigned char, 10> expected;
    std::array<unsigned char, 10> replacement;
    size_t size;
};

const Patch kPatches[] = {
    // 0052A934: MOV EAX,0x200 -> MOV EAX,0x800
    {kPatchAddressMovEax,
     {0xB8, 0x00, 0x02, 0x00, 0x00},
     {0xB8, 0x00, 0x08, 0x00, 0x00},
     5},
    // 0052A94C: MOV [00E0498C],0x200 -> MOV [00E0498C],0x800
    {kPatchAddressWidthStore,
     {0xC7, 0x05, 0x8C, 0x49, 0xE0, 0x00, 0x00, 0x02, 0x00, 0x00},
     {0xC7, 0x05, 0x8C, 0x49, 0xE0, 0x00, 0x00, 0x08, 0x00, 0x00},
     10},
    // 0052A956: MOV [00E04990],0x100 -> MOV [00E04990],0x400
    {kPatchAddressHeightStore,
     {0xC7, 0x05, 0x90, 0x49, 0xE0, 0x00, 0x00, 0x01, 0x00, 0x00},
     {0xC7, 0x05, 0x90, 0x49, 0xE0, 0x00, 0x00, 0x04, 0x00, 0x00},
     10},
};

bool BytesEqual(const unsigned char* current, const std::array<unsigned char, 10>& expected, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (current[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

void ApplyPatch(const Patch& patch) {
    auto* address = reinterpret_cast<unsigned char*>(patch.virtualAddress);
    if (!BytesEqual(address, patch.expected, patch.size) &&
        !BytesEqual(address, patch.replacement, patch.size)) {
        return;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(address, patch.size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return;
    }

    for (size_t i = 0; i < patch.size; ++i) {
        address[i] = patch.replacement[i];
    }
    FlushInstructionCache(GetCurrentProcess(), address, patch.size);

    DWORD ignored = 0;
    VirtualProtect(address, patch.size, oldProtect, &ignored);
}

void ApplyBhdPatches() {
    if (reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) != kImageBase) {
        return;
    }

    for (const auto& patch : kPatches) {
        ApplyPatch(patch);
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
