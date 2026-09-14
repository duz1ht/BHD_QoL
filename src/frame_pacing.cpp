#include "frame_pacing.h"

#include <d3d8.h>

#include <cstdint>
#include <cstring>

#include "logger.h"

namespace frame_pacing {
namespace {
constexpr size_t kCreateDeviceVtableIndex = 15;
constexpr size_t kResetVtableIndex = 14;
constexpr size_t kPresentVtableIndex = 15;

using Direct3DCreate8Fn = IDirect3D8*(WINAPI*)(UINT);
using CreateDeviceFn = HRESULT(WINAPI*)(IDirect3D8*, UINT, D3DDEVTYPE, HWND, DWORD,
                                        D3DPRESENT_PARAMETERS*, IDirect3DDevice8**);
using ResetFn = HRESULT(WINAPI*)(IDirect3DDevice8*, D3DPRESENT_PARAMETERS*);
using PresentFn = HRESULT(WINAPI*)(IDirect3DDevice8*, const RECT*, const RECT*, HWND,
                                   const RGNDATA*);

Direct3DCreate8Fn g_realDirect3DCreate8 = nullptr;
CreateDeviceFn g_realCreateDevice = nullptr;
ResetFn g_realReset = nullptr;
PresentFn g_realPresent = nullptr;
bool g_enabled = false;
DWORD g_statisticsIntervalMs = 5000;
DWORD g_lastStatisticsTick = 0;
LARGE_INTEGER g_counterFrequency = {};

alignas(8) volatile LONG64 g_lastPresentTick = 0;
alignas(8) volatile LONG64 g_lastInputPollTick = 0;
alignas(8) volatile LONG64 g_lastReportedInputPollTick = 0;
alignas(8) volatile LONG64 g_totalFrameUs = 0;
alignas(8) volatile LONG64 g_maxFrameUs = 0;
alignas(8) volatile LONG64 g_totalPollToPresentUs = 0;
alignas(8) volatile LONG64 g_maxPollToPresentUs = 0;
volatile LONG g_frameIntervals = 0;
volatile LONG g_presentCalls = 0;
volatile LONG g_presentFailures = 0;
volatile LONG g_pollToPresentSamples = 0;
volatile LONG g_presentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
volatile LONG g_windowed = 0;
volatile LONG g_backBufferWidth = 0;
volatile LONG g_backBufferHeight = 0;
volatile LONG g_fullScreenRefreshRate = 0;

LONG64 CounterNow() {
    LARGE_INTEGER value = {};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

LONG64 TicksToMicroseconds(LONG64 ticks) {
    if (ticks <= 0 || g_counterFrequency.QuadPart <= 0) return 0;
    const LONG64 seconds = ticks / g_counterFrequency.QuadPart;
    const LONG64 remainder = ticks % g_counterFrequency.QuadPart;
    return seconds * 1000000 + remainder * 1000000 / g_counterFrequency.QuadPart;
}

void UpdateMaximum(volatile LONG64* destination, LONG64 value) {
    LONG64 observed = InterlockedCompareExchange64(destination, 0, 0);
    while (value > observed) {
        const LONG64 previous = InterlockedCompareExchange64(destination, value, observed);
        if (previous == observed) break;
        observed = previous;
    }
}

const char* PresentationIntervalName(UINT interval) {
    switch (interval) {
        case D3DPRESENT_INTERVAL_DEFAULT: return "default";
        case D3DPRESENT_INTERVAL_ONE: return "one";
        case D3DPRESENT_INTERVAL_TWO: return "two";
        case D3DPRESENT_INTERVAL_THREE: return "three";
        case D3DPRESENT_INTERVAL_FOUR: return "four";
        case D3DPRESENT_INTERVAL_IMMEDIATE: return "immediate";
        default: return "unknown";
    }
}

void CapturePresentationParameters(const D3DPRESENT_PARAMETERS* parameters,
                                   const char* trigger) {
    if (parameters == nullptr) return;
    InterlockedExchange(&g_presentationInterval,
                        static_cast<LONG>(parameters->FullScreen_PresentationInterval));
    InterlockedExchange(&g_windowed, parameters->Windowed ? 1 : 0);
    InterlockedExchange(&g_backBufferWidth, static_cast<LONG>(parameters->BackBufferWidth));
    InterlockedExchange(&g_backBufferHeight, static_cast<LONG>(parameters->BackBufferHeight));
    InterlockedExchange(&g_fullScreenRefreshRate,
                        static_cast<LONG>(parameters->FullScreen_RefreshRateInHz));
    logger::Log("INFO", "FramePacing.Config",
                "trigger=%s windowed=%d back_buffer=%ux%u refresh_hz=%u "
                "presentation_interval=%s(0x%08X) swap_effect=%u back_buffer_count=%u",
                trigger, parameters->Windowed, parameters->BackBufferWidth,
                parameters->BackBufferHeight, parameters->FullScreen_RefreshRateInHz,
                PresentationIntervalName(parameters->FullScreen_PresentationInterval),
                parameters->FullScreen_PresentationInterval, parameters->SwapEffect,
                parameters->BackBufferCount);
}

void ReportStatistics() {
    const LONG frameIntervals = InterlockedExchange(&g_frameIntervals, 0);
    const LONG presents = InterlockedExchange(&g_presentCalls, 0);
    const LONG failures = InterlockedExchange(&g_presentFailures, 0);
    const LONG64 totalFrameUs = InterlockedExchange64(&g_totalFrameUs, 0);
    const LONG64 maxFrameUs = InterlockedExchange64(&g_maxFrameUs, 0);
    const LONG pollSamples = InterlockedExchange(&g_pollToPresentSamples, 0);
    const LONG64 totalPollToPresentUs = InterlockedExchange64(&g_totalPollToPresentUs, 0);
    const LONG64 maxPollToPresentUs = InterlockedExchange64(&g_maxPollToPresentUs, 0);
    const LONG64 averageFrameUs = frameIntervals ? totalFrameUs / frameIntervals : 0;
    const LONG64 estimatedMilliHz = averageFrameUs ? 1000000000 / averageFrameUs : 0;
    const UINT interval = static_cast<UINT>(
        InterlockedCompareExchange(&g_presentationInterval, 0, 0));
    logger::Log("INFO", "FramePacing.Stats",
                "interval_ms=%lu presents=%ld failures=%ld frame_samples=%ld "
                "frame_avg_us=%lld frame_max_us=%lld estimated_fps_millihz=%lld "
                "poll_to_present_samples=%ld poll_to_present_avg_us=%lld "
                "poll_to_present_max_us=%lld windowed=%ld back_buffer=%ldx%ld "
                "refresh_hz=%ld presentation_interval=%s(0x%08X)",
                g_statisticsIntervalMs, presents, failures, frameIntervals, averageFrameUs,
                maxFrameUs, estimatedMilliHz, pollSamples,
                pollSamples ? totalPollToPresentUs / pollSamples : 0,
                maxPollToPresentUs, InterlockedCompareExchange(&g_windowed, 0, 0),
                InterlockedCompareExchange(&g_backBufferWidth, 0, 0),
                InterlockedCompareExchange(&g_backBufferHeight, 0, 0),
                InterlockedCompareExchange(&g_fullScreenRefreshRate, 0, 0),
                PresentationIntervalName(interval), interval);
}

bool PatchPointer(void** slot, void* replacement, void** original) {
    if (slot == nullptr || replacement == nullptr || original == nullptr) return false;
    if (*slot == replacement) return true;
    // Do not make one global trampoline service an unrelated D3D8 wrapper
    // implementation. The game uses one device implementation in practice,
    // but failing closed is safer than calling a method from another vtable.
    if (*original != nullptr && *slot != *original) return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) return false;
    *original = *slot;
    InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(slot), replacement);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    return true;
}

HRESULT WINAPI HookedPresent(IDirect3DDevice8* device, const RECT* source,
                             const RECT* destination, HWND overrideWindow,
                             const RGNDATA* dirtyRegion) {
    const LONG64 presentTick = CounterNow();
    const LONG64 previousPresent = InterlockedExchange64(&g_lastPresentTick, presentTick);
    if (previousPresent != 0) {
        const LONG64 frameUs = TicksToMicroseconds(presentTick - previousPresent);
        InterlockedExchangeAdd64(&g_totalFrameUs, frameUs);
        UpdateMaximum(&g_maxFrameUs, frameUs);
        InterlockedIncrement(&g_frameIntervals);
    }

    const LONG64 pollTick = InterlockedCompareExchange64(&g_lastInputPollTick, 0, 0);
    const LONG64 reportedPoll = InterlockedCompareExchange64(&g_lastReportedInputPollTick, 0, 0);
    if (pollTick != 0 && pollTick != reportedPoll && presentTick >= pollTick &&
        InterlockedCompareExchange64(&g_lastReportedInputPollTick, pollTick, reportedPoll) ==
            reportedPoll) {
        const LONG64 latencyUs = TicksToMicroseconds(presentTick - pollTick);
        InterlockedExchangeAdd64(&g_totalPollToPresentUs, latencyUs);
        UpdateMaximum(&g_maxPollToPresentUs, latencyUs);
        InterlockedIncrement(&g_pollToPresentSamples);
    }

    InterlockedIncrement(&g_presentCalls);
    const HRESULT result = g_realPresent(device, source, destination, overrideWindow, dirtyRegion);
    if (FAILED(result)) InterlockedIncrement(&g_presentFailures);
    const DWORD now = GetTickCount();
    if (now - g_lastStatisticsTick >= g_statisticsIntervalMs) {
        g_lastStatisticsTick = now;
        ReportStatistics();
    }
    return result;
}

HRESULT WINAPI HookedReset(IDirect3DDevice8* device, D3DPRESENT_PARAMETERS* parameters) {
    const HRESULT result = g_realReset(device, parameters);
    if (SUCCEEDED(result)) {
        CapturePresentationParameters(parameters, "Reset");
        InterlockedExchange64(&g_lastPresentTick, 0);
    }
    return result;
}

bool HookDevice(IDirect3DDevice8* device) {
    if (device == nullptr) return false;
    void** vtable = *reinterpret_cast<void***>(device);
    const bool resetHooked = PatchPointer(&vtable[kResetVtableIndex],
                                          reinterpret_cast<void*>(&HookedReset),
                                          reinterpret_cast<void**>(&g_realReset));
    const bool presentHooked = PatchPointer(&vtable[kPresentVtableIndex],
                                            reinterpret_cast<void*>(&HookedPresent),
                                            reinterpret_cast<void**>(&g_realPresent));
    logger::Log(resetHooked && presentHooked ? "INFO" : "ERROR", "FramePacing",
                "device hooks reset=%d present=%d device=0x%08lX",
                resetHooked, presentHooked, reinterpret_cast<unsigned long>(device));
    return resetHooked && presentHooked;
}

HRESULT WINAPI HookedCreateDevice(IDirect3D8* direct3D, UINT adapter, D3DDEVTYPE deviceType,
                                  HWND focusWindow, DWORD behaviorFlags,
                                  D3DPRESENT_PARAMETERS* parameters,
                                  IDirect3DDevice8** returnedDevice) {
    const HRESULT result = g_realCreateDevice(direct3D, adapter, deviceType, focusWindow,
                                               behaviorFlags, parameters, returnedDevice);
    if (SUCCEEDED(result)) {
        CapturePresentationParameters(parameters, "CreateDevice");
        HookDevice(returnedDevice == nullptr ? nullptr : *returnedDevice);
    } else {
        logger::Log("ERROR", "FramePacing", "CreateDevice failed result=0x%08lX",
                    static_cast<unsigned long>(result));
    }
    return result;
}

IDirect3D8* WINAPI HookedDirect3DCreate8(UINT sdkVersion) {
    IDirect3D8* direct3D = g_realDirect3DCreate8(sdkVersion);
    if (direct3D == nullptr) {
        logger::Log("ERROR", "FramePacing", "Direct3DCreate8 returned null sdk=%u", sdkVersion);
        return nullptr;
    }
    void** vtable = *reinterpret_cast<void***>(direct3D);
    const bool hooked = PatchPointer(&vtable[kCreateDeviceVtableIndex],
                                     reinterpret_cast<void*>(&HookedCreateDevice),
                                     reinterpret_cast<void**>(&g_realCreateDevice));
    logger::Log(hooked ? "INFO" : "ERROR", "FramePacing",
                "CreateDevice hook=%d interface=0x%08lX", hooked,
                reinterpret_cast<unsigned long>(direct3D));
    return direct3D;
}

bool HookDirect3DCreate8Import() {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const IMAGE_DATA_DIRECTORY& directory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0 || directory.Size == 0) return false;

    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor) {
        const char* moduleName = reinterpret_cast<const char*>(base + descriptor->Name);
        if (lstrcmpiA(moduleName, "d3d8.dll") != 0 || descriptor->OriginalFirstThunk == 0) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import->Name), "Direct3DCreate8") != 0)
                continue;
            auto** slot = reinterpret_cast<void**>(&addresses->u1.Function);
            return PatchPointer(slot, reinterpret_cast<void*>(&HookedDirect3DCreate8),
                                reinterpret_cast<void**>(&g_realDirect3DCreate8));
        }
    }
    return false;
}
}  // namespace

bool Install(const Settings& settings) {
    if (!settings.enabled) {
        logger::Log("INFO", "FramePacing", "diagnostics disabled");
        return true;
    }
    if (g_enabled) return true;
    g_statisticsIntervalMs = settings.statisticsIntervalMs;
    g_lastStatisticsTick = GetTickCount();
    if (!QueryPerformanceFrequency(&g_counterFrequency) ||
        !HookDirect3DCreate8Import()) {
        logger::Log("ERROR", "FramePacing",
                    "could not install Direct3DCreate8 import hook error=%lu", GetLastError());
        return false;
    }
    g_enabled = true;
    logger::Log("INFO", "FramePacing", "Direct3DCreate8 import hook installed");
    return true;
}

void NotifyInputPoll(LONG64 counterTick) {
    if (g_enabled) InterlockedExchange64(&g_lastInputPollTick, counterTick);
}

}  // namespace frame_pacing
