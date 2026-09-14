#include "frame_pacing.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "logger.h"
#include "frame_pacing_math.h"

// The diagnostics only need the stable Direct3D 8 COM ABI and presentation
// structure. Defining that small surface locally avoids requiring the legacy
// DirectX SDK's d3d8.h when building with current Visual Studio installations.
struct IDirect3D8;
struct IDirect3DDevice8;

using D3DDEVTYPE = UINT;
using D3DFORMAT = UINT;
using D3DMULTISAMPLE_TYPE = UINT;
using D3DSWAPEFFECT = UINT;

struct D3DPRESENT_PARAMETERS {
    UINT BackBufferWidth;
    UINT BackBufferHeight;
    D3DFORMAT BackBufferFormat;
    UINT BackBufferCount;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    D3DSWAPEFFECT SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed;
    BOOL EnableAutoDepthStencil;
    D3DFORMAT AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz;
    UINT FullScreen_PresentationInterval;
};

static_assert(sizeof(void*) == 4, "BHD QoL must be built as a 32-bit DLL");
static_assert(sizeof(D3DPRESENT_PARAMETERS) == 52,
              "Unexpected Direct3D 8 presentation-parameter ABI");
static_assert(offsetof(D3DPRESENT_PARAMETERS, FullScreen_PresentationInterval) == 48,
              "Unexpected Direct3D 8 presentation-interval offset");

constexpr UINT D3DPRESENT_INTERVAL_DEFAULT = 0x00000000;
constexpr UINT D3DPRESENT_INTERVAL_ONE = 0x00000001;
constexpr UINT D3DPRESENT_INTERVAL_TWO = 0x00000002;
constexpr UINT D3DPRESENT_INTERVAL_THREE = 0x00000004;
constexpr UINT D3DPRESENT_INTERVAL_FOUR = 0x00000008;
constexpr UINT D3DPRESENT_INTERVAL_IMMEDIATE = 0x80000000u;

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
bool g_diagnosticsEnabled = false;
UINT g_renderFrameLimit = 0;
LONG64 g_limitPeriodTicks = 0;
alignas(8) volatile LONG64 g_nextFrameDeadline = 0;
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
volatile LONG g_inputPolls = 0;
alignas(8) volatile LONG64 g_totalPresentCallUs = 0;
alignas(8) volatile LONG64 g_maxPresentCallUs = 0;
volatile LONG g_presentCallSamples = 0;
volatile LONG g_presentCallsOver2Ms = 0;
volatile LONG g_presentCallsOver8Ms = 0;
volatile LONG g_presentCallsOver16Ms = 0;
volatile LONG g_presentCallsOver33Ms = 0;
volatile LONG g_stallCount = 0;
volatile LONG g_frameLimitWaits = 0;
volatile LONG g_frameLimitMisses = 0;
alignas(8) volatile LONG64 g_totalFrameLimitWaitUs = 0;
alignas(8) volatile LONG64 g_maxFrameLimitWaitUs = 0;
alignas(8) volatile LONG64 g_maxStallUs = 0;
alignas(8) volatile LONG64 g_statisticsWindowStartTick = 0;
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

void ResetStatisticsWindow() {
    InterlockedExchange64(&g_lastPresentTick, 0);
    InterlockedExchange64(&g_totalFrameUs, 0);
    InterlockedExchange64(&g_maxFrameUs, 0);
    InterlockedExchange64(&g_totalPollToPresentUs, 0);
    InterlockedExchange64(&g_maxPollToPresentUs, 0);
    InterlockedExchange64(&g_totalPresentCallUs, 0);
    InterlockedExchange64(&g_maxPresentCallUs, 0);
    InterlockedExchange64(&g_maxStallUs, 0);
    InterlockedExchange(&g_frameIntervals, 0);
    InterlockedExchange(&g_presentCalls, 0);
    InterlockedExchange(&g_presentFailures, 0);
    InterlockedExchange(&g_pollToPresentSamples, 0);
    InterlockedExchange(&g_inputPolls, 0);
    InterlockedExchange(&g_presentCallSamples, 0);
    InterlockedExchange(&g_presentCallsOver2Ms, 0);
    InterlockedExchange(&g_presentCallsOver8Ms, 0);
    InterlockedExchange(&g_presentCallsOver16Ms, 0);
    InterlockedExchange(&g_presentCallsOver33Ms, 0);
    InterlockedExchange(&g_stallCount, 0);
    InterlockedExchange(&g_frameLimitWaits, 0);
    InterlockedExchange(&g_frameLimitMisses, 0);
    InterlockedExchange64(&g_totalFrameLimitWaitUs, 0);
    InterlockedExchange64(&g_maxFrameLimitWaitUs, 0);
    InterlockedExchange64(&g_statisticsWindowStartTick, CounterNow());
    g_lastStatisticsTick = GetTickCount();
}

void ApplyFrameLimit() {
    if (g_limitPeriodTicks <= 0) return;
    LONG64 now = CounterNow();
    LONG64 deadline = InterlockedCompareExchange64(&g_nextFrameDeadline, 0, 0);
    if (deadline == 0 || now > deadline + g_limitPeriodTicks) {
        InterlockedExchange64(&g_nextFrameDeadline, now + g_limitPeriodTicks);
        if (deadline != 0) InterlockedIncrement(&g_frameLimitMisses);
        return;
    }

    const LONG64 waitStart = now;
    while (now < deadline) {
        const LONG64 remainingUs = TicksToMicroseconds(deadline - now);
        if (remainingUs > 2000) {
            Sleep(static_cast<DWORD>((remainingUs - 1000) / 1000));
        } else {
            SwitchToThread();
        }
        now = CounterNow();
    }
    const LONG64 waitedUs = TicksToMicroseconds(now - waitStart);
    InterlockedIncrement(&g_frameLimitWaits);
    InterlockedExchangeAdd64(&g_totalFrameLimitWaitUs, waitedUs);
    UpdateMaximum(&g_maxFrameLimitWaitUs, waitedUs);
    InterlockedExchange64(&g_nextFrameDeadline, deadline + g_limitPeriodTicks);
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
    const LONG64 reportTick = CounterNow();
    const LONG64 windowStart = InterlockedExchange64(&g_statisticsWindowStartTick, reportTick);
    const LONG64 actualIntervalMs =
        windowStart ? TicksToMicroseconds(reportTick - windowStart) / 1000 : 0;
    const LONG frameIntervals = InterlockedExchange(&g_frameIntervals, 0);
    const LONG presents = InterlockedExchange(&g_presentCalls, 0);
    const LONG failures = InterlockedExchange(&g_presentFailures, 0);
    const LONG64 totalFrameUs = InterlockedExchange64(&g_totalFrameUs, 0);
    const LONG64 maxFrameUs = InterlockedExchange64(&g_maxFrameUs, 0);
    const LONG pollSamples = InterlockedExchange(&g_pollToPresentSamples, 0);
    const LONG inputPolls = InterlockedExchange(&g_inputPolls, 0);
    const LONG64 totalPollToPresentUs = InterlockedExchange64(&g_totalPollToPresentUs, 0);
    const LONG64 maxPollToPresentUs = InterlockedExchange64(&g_maxPollToPresentUs, 0);
    const LONG presentCallSamples = InterlockedExchange(&g_presentCallSamples, 0);
    const LONG64 totalPresentCallUs = InterlockedExchange64(&g_totalPresentCallUs, 0);
    const LONG64 maxPresentCallUs = InterlockedExchange64(&g_maxPresentCallUs, 0);
    const LONG callsOver2Ms = InterlockedExchange(&g_presentCallsOver2Ms, 0);
    const LONG callsOver8Ms = InterlockedExchange(&g_presentCallsOver8Ms, 0);
    const LONG callsOver16Ms = InterlockedExchange(&g_presentCallsOver16Ms, 0);
    const LONG callsOver33Ms = InterlockedExchange(&g_presentCallsOver33Ms, 0);
    const LONG stalls = InterlockedExchange(&g_stallCount, 0);
    const LONG64 maxStallUs = InterlockedExchange64(&g_maxStallUs, 0);
    const LONG limitWaits = InterlockedExchange(&g_frameLimitWaits, 0);
    const LONG limitMisses = InterlockedExchange(&g_frameLimitMisses, 0);
    const LONG64 totalLimitWaitUs = InterlockedExchange64(&g_totalFrameLimitWaitUs, 0);
    const LONG64 maxLimitWaitUs = InterlockedExchange64(&g_maxFrameLimitWaitUs, 0);
    const LONG64 averageFrameUs = AverageOrZero(totalFrameUs, frameIntervals);
    const LONG64 estimatedMilliHz = averageFrameUs ? 1000000000 / averageFrameUs : 0;
    const LONG64 presentRateMilliHz = RateMilliHz(presents, actualIntervalMs);
    const LONG64 presentsPerPollMilli = inputPolls > 0
        ? static_cast<LONG64>(presents) * 1000 / inputPolls : 0;
    const UINT interval = static_cast<UINT>(
        InterlockedCompareExchange(&g_presentationInterval, 0, 0));
    logger::Log("INFO", "FramePacing.Stats",
                "configured_interval_ms=%lu actual_interval_ms=%lld presents=%ld failures=%ld frame_samples=%ld "
                "frame_avg_us=%lld frame_max_us=%lld estimated_fps_millihz=%lld present_rate_millihz=%lld "
                "input_polls=%ld presents_per_poll_milli=%lld "
                "poll_to_present_samples=%ld poll_to_present_avg_us=%lld "
                "poll_to_present_max_us=%lld present_call_samples=%ld present_call_avg_us=%lld "
                "present_call_max_us=%lld present_over_2ms=%ld present_over_8ms=%ld "
                "present_over_16ms=%ld present_over_33ms=%ld stalls=%ld stall_max_us=%lld "
                "frame_limit=%u limit_waits=%ld limit_wait_avg_us=%lld "
                "limit_wait_max_us=%lld limit_misses=%ld "
                "windowed=%ld back_buffer=%ldx%ld "
                "refresh_hz=%ld presentation_interval=%s(0x%08X)",
                g_statisticsIntervalMs, actualIntervalMs, presents, failures, frameIntervals, averageFrameUs,
                maxFrameUs, estimatedMilliHz, presentRateMilliHz, inputPolls,
                presentsPerPollMilli, pollSamples,
                pollSamples ? totalPollToPresentUs / pollSamples : 0,
                maxPollToPresentUs, presentCallSamples,
                presentCallSamples ? totalPresentCallUs / presentCallSamples : 0,
                maxPresentCallUs, callsOver2Ms, callsOver8Ms, callsOver16Ms,
                callsOver33Ms, stalls, maxStallUs, g_renderFrameLimit, limitWaits,
                AverageOrZero(totalLimitWaitUs, limitWaits), maxLimitWaitUs, limitMisses,
                InterlockedCompareExchange(&g_windowed, 0, 0),
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
    ApplyFrameLimit();
    const LONG64 presentTick = CounterNow();
    const LONG64 previousPresent = InterlockedExchange64(&g_lastPresentTick, presentTick);
    if (previousPresent != 0) {
        const LONG64 frameUs = TicksToMicroseconds(presentTick - previousPresent);
        if (IsFrameStall(frameUs)) {
            InterlockedIncrement(&g_stallCount);
            UpdateMaximum(&g_maxStallUs, frameUs);
        } else {
            InterlockedExchangeAdd64(&g_totalFrameUs, frameUs);
            UpdateMaximum(&g_maxFrameUs, frameUs);
            InterlockedIncrement(&g_frameIntervals);
        }
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
    const LONG64 callStartTick = CounterNow();
    const HRESULT result = g_realPresent(device, source, destination, overrideWindow, dirtyRegion);
    const LONG64 callUs = TicksToMicroseconds(CounterNow() - callStartTick);
    InterlockedExchangeAdd64(&g_totalPresentCallUs, callUs);
    UpdateMaximum(&g_maxPresentCallUs, callUs);
    InterlockedIncrement(&g_presentCallSamples);
    if (callUs >= 2000) InterlockedIncrement(&g_presentCallsOver2Ms);
    if (callUs >= 8000) InterlockedIncrement(&g_presentCallsOver8Ms);
    if (callUs >= 16000) InterlockedIncrement(&g_presentCallsOver16Ms);
    if (callUs >= 33000) InterlockedIncrement(&g_presentCallsOver33Ms);
    if (FAILED(result)) InterlockedIncrement(&g_presentFailures);
    const DWORD now = GetTickCount();
    if (g_diagnosticsEnabled && now - g_lastStatisticsTick >= g_statisticsIntervalMs) {
        g_lastStatisticsTick = now;
        ReportStatistics();
    }
    return result;
}

HRESULT WINAPI HookedReset(IDirect3DDevice8* device, D3DPRESENT_PARAMETERS* parameters) {
    const HRESULT result = g_realReset(device, parameters);
    if (SUCCEEDED(result)) {
        CapturePresentationParameters(parameters, "Reset");
        ResetStatisticsWindow();
        InterlockedExchange64(&g_nextFrameDeadline, 0);
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
    if (!settings.diagnosticsEnabled && settings.renderFrameLimit == 0) {
        logger::Log("INFO", "FramePacing", "diagnostics disabled");
        return true;
    }
    if (g_enabled) return true;
    g_statisticsIntervalMs = settings.statisticsIntervalMs;
    g_diagnosticsEnabled = settings.diagnosticsEnabled;
    g_renderFrameLimit = settings.renderFrameLimit;
    if (!QueryPerformanceFrequency(&g_counterFrequency)) {
        logger::Log("ERROR", "FramePacing", "QueryPerformanceFrequency failed");
        return false;
    }
    g_limitPeriodTicks = g_renderFrameLimit > 0
        ? g_counterFrequency.QuadPart / g_renderFrameLimit : 0;
    ResetStatisticsWindow();
    if (!HookDirect3DCreate8Import()) {
        logger::Log("ERROR", "FramePacing",
                    "could not install Direct3DCreate8 import hook error=%lu", GetLastError());
        return false;
    }
    g_enabled = true;
    logger::Log("INFO", "FramePacing",
                "Direct3DCreate8 import hook installed diagnostics=%d frame_limit=%u",
                g_diagnosticsEnabled, g_renderFrameLimit);
    return true;
}

void NotifyInputPoll(LONG64 counterTick) {
    if (g_enabled && g_diagnosticsEnabled) {
        InterlockedExchange64(&g_lastInputPollTick, counterTick);
        InterlockedIncrement(&g_inputPolls);
    }
}

}  // namespace frame_pacing
