#include "d3d8_resolution_reset.h"

#include <cstdint>
#include <cstring>

#include "logger.h"

namespace d3d8_resolution_reset {
namespace {
constexpr uintptr_t kCompleteVideoModeTransition = 0x004E6B10;
constexpr uintptr_t kScreenWidth = 0x009F72C0;
constexpr uintptr_t kScreenHeight = 0x009F72C4;
const unsigned char kExpectedTransition[] = {
    0xE8, 0xAB, 0x0D, 0xFF, 0xFF, 0xA1, 0x1C, 0x42, 0xA3, 0x00,
};

// The proxy only forwards opaque COM interface and presentation-parameter pointers. Keeping
// these signatures ABI-only avoids requiring the legacy DirectX 8 SDK's d3d8.h header.
using Direct3DCreate8Function = void* (WINAPI*)(UINT);
using CreateDeviceFunction = HRESULT (STDMETHODCALLTYPE*)(void*, UINT, DWORD,
    HWND, DWORD, void*, void**);
using PresentFunction = HRESULT (STDMETHODCALLTYPE*)(void*, const RECT*,
    const RECT*, HWND, const void*);

Direct3DCreate8Function g_originalDirect3DCreate8 = nullptr;
CreateDeviceFunction g_originalCreateDevice = nullptr;
PresentFunction g_originalPresent = nullptr;
volatile LONG g_enabled = 0;
volatile LONG g_active = 0;
volatile LONG g_pending = 0;
volatile LONG g_transitioning = 0;
volatile LONG g_width = 0;
volatile LONG g_height = 0;
volatile LONG g_generation = 0;
volatile LONG g_activeFrames = 0;

bool PatchPointer(void** slot, void* replacement, void** original) {
    DWORD oldProtection = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtection)) return false;
    if (original != nullptr && *original == nullptr) *original = *slot;
    *slot = replacement;
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(slot, sizeof(*slot), oldProtection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    return restored != FALSE;
}

HRESULT STDMETHODCALLTYPE HookPresent(void* device, const RECT* source,
                                      const RECT* destination, HWND overrideWindow,
                                      const void* dirtyRegion) {
    if (InterlockedCompareExchange(&g_active, 0, 0) != 0) {
        InterlockedIncrement(&g_activeFrames);
    } else {
        InterlockedExchange(&g_activeFrames, 0);
    }

    if (InterlockedCompareExchange(&g_pending, 0, 0) != 0 &&
        InterlockedCompareExchange(&g_activeFrames, 0, 0) >= 2 &&
        InterlockedCompareExchange(&g_transitioning, 1, 0) == 0) {
        const LONG generation = InterlockedCompareExchange(&g_generation, 0, 0);
        const LONG requestedWidth = InterlockedCompareExchange(&g_width, 0, 0);
        const LONG requestedHeight = InterlockedCompareExchange(&g_height, 0, 0);
        InterlockedExchange(&g_pending, 0);
        logger::Log("INFO", "D3D8ResolutionReset",
                    "generation=%ld transition=started requested=%ldx%ld",
                    generation, requestedWidth, requestedHeight);

        using CompleteVideoModeTransition = void (__cdecl*)();
        reinterpret_cast<CompleteVideoModeTransition>(kCompleteVideoModeTransition)();

        const LONG actualWidth = *reinterpret_cast<volatile LONG*>(kScreenWidth);
        const LONG actualHeight = *reinterpret_cast<volatile LONG*>(kScreenHeight);
        const bool completed = actualWidth == requestedWidth && actualHeight == requestedHeight;
        logger::Log(completed ? "INFO" : "ERROR", "D3D8ResolutionReset",
                    "generation=%ld transition=%s requested=%ldx%ld actual=%ldx%ld",
                    generation, completed ? "completed" : "failed", requestedWidth,
                    requestedHeight, actualWidth, actualHeight);
        // The native transition already performs its own mode fallbacks. Do not retry a
        // failed generation every frame; a later desktop change will create a new request.
        if (generation != InterlockedCompareExchange(&g_generation, 0, 0))
            InterlockedExchange(&g_pending, 1);
        InterlockedExchange(&g_transitioning, 0);
        // The native transition may have released this device. Never Present through
        // the stale interface from the frame that performed the recreation.
        return S_OK;
    }
    return g_originalPresent(device, source, destination, overrideWindow, dirtyRegion);
}

void HookDevice(void* device) {
    if (device == nullptr) return;
    void** vtable = *reinterpret_cast<void***>(device);
    if (!PatchPointer(&vtable[15], reinterpret_cast<void*>(&HookPresent),
                      reinterpret_cast<void**>(&g_originalPresent))) {
        logger::Log("ERROR", "D3D8ResolutionReset", "could not hook IDirect3DDevice8::Present");
    }
}

HRESULT STDMETHODCALLTYPE HookCreateDevice(void* direct3d, UINT adapter,
    DWORD type, HWND focusWindow, DWORD behavior, void* parameters, void** device) {
    const HRESULT result = g_originalCreateDevice(direct3d, adapter, type, focusWindow,
                                                   behavior, parameters, device);
    if (SUCCEEDED(result) && device != nullptr) HookDevice(*device);
    return result;
}

void* WINAPI HookDirect3DCreate8(UINT sdkVersion) {
    void* direct3d = g_originalDirect3DCreate8(sdkVersion);
    if (direct3d == nullptr) return nullptr;
    void** vtable = *reinterpret_cast<void***>(direct3d);
    if (!PatchPointer(&vtable[15], reinterpret_cast<void*>(&HookCreateDevice),
                      reinterpret_cast<void**>(&g_originalCreateDevice))) {
        logger::Log("ERROR", "D3D8ResolutionReset", "could not hook IDirect3D8::CreateDevice");
    }
    return direct3d;
}

void** FindImportSlot(const char* dllName, const char* functionName) {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (base == nullptr) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const DWORD importRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (importRva == 0) return nullptr;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importRva);
    for (; descriptor->Name != 0; ++descriptor) {
        const char* importedDll = reinterpret_cast<const char*>(base + descriptor->Name);
        if (lstrcmpiA(importedDll, dllName) != 0) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import->Name), functionName) == 0)
                return reinterpret_cast<void**>(&addresses->u1.Function);
        }
    }
    return nullptr;
}
}  // namespace

bool Initialize(bool enabled) {
    if (!enabled) return true;
    if (std::memcmp(reinterpret_cast<const void*>(kCompleteVideoModeTransition),
                    kExpectedTransition, sizeof(kExpectedTransition)) != 0) {
        logger::Log("ERROR", "D3D8ResolutionReset", "video-mode transition signature mismatch");
        return false;
    }
    void** slot = FindImportSlot("d3d8.dll", "Direct3DCreate8");
    if (slot == nullptr || !PatchPointer(slot, reinterpret_cast<void*>(&HookDirect3DCreate8),
                                        reinterpret_cast<void**>(&g_originalDirect3DCreate8))) {
        logger::Log("ERROR", "D3D8ResolutionReset", "could not hook Direct3DCreate8 import");
        return false;
    }
    InterlockedExchange(&g_enabled, 1);
    logger::Log("INFO", "D3D8ResolutionReset", "runtime resolution transitions enabled");
    return true;
}

void Request(LONG width, LONG height) {
    if (InterlockedCompareExchange(&g_enabled, 0, 0) == 0 || width <= 0 || height <= 0) return;
    InterlockedExchange(&g_width, width);
    InterlockedExchange(&g_height, height);
    const LONG generation = InterlockedIncrement(&g_generation);
    InterlockedExchange(&g_pending, 1);
    InterlockedExchange(&g_activeFrames, 0);
    logger::Log("INFO", "D3D8ResolutionReset", "generation=%ld requested=%ldx%ld",
                generation, width, height);
}

void SetGameActive(bool active) {
    InterlockedExchange(&g_active, active ? 1 : 0);
    if (!active) InterlockedExchange(&g_activeFrames, 0);
}

}  // namespace d3d8_resolution_reset
