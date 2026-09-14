#include "raw_input.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game_window.h"
#include "frame_pacing.h"
#include "cursor_clip.h"
#include "logger.h"
#include "mouse_scaling_fix.h"

namespace raw_input {
namespace {
constexpr uintptr_t kPollMouseInput = 0x005678F0;
constexpr uintptr_t kMouseDispatcher = 0x00567710;
constexpr uintptr_t kGameWindow = 0x00F654FC;
constexpr uintptr_t kCursorX = 0x00F655E0;
constexpr uintptr_t kCursorY = 0x00F655E4;
constexpr uintptr_t kMouseState = 0x00F655E8;
constexpr uintptr_t kRelativeX = 0x00F655EC;
constexpr uintptr_t kRelativeY = 0x00F655F0;
constexpr uintptr_t kScreenWidth = 0x009F72C0;
constexpr uintptr_t kScreenHeight = 0x009F72C4;

constexpr unsigned char kExpectedPollBytes[] = {0x83, 0xEC, 0x24, 0x56, 0x8B, 0x35};
constexpr size_t kDetourSize = sizeof(kExpectedPollBytes);
using PollMouseInputFn = void(__cdecl*)();
using MouseDispatcherFn = void(__cdecl*)(WPARAM, LPARAM, UINT, int);

volatile LONG g_accumX = 0;
volatile LONG g_accumY = 0;
volatile LONG g_buttonState = 0;
enum BackendState : LONG { kInactive = 0, kRecoveryPending = 1, kActive = 2 };
volatile LONG g_backendState = kInactive;
volatile LONG g_registered = 0;
volatile LONG g_initializing = 0;
volatile LONG g_activatedOnce = 0;
volatile LONG g_startupLegacyFallbackLogged = 0;
volatile LONG g_awaitingFirstPoll = 1;
volatile LONG g_prePollReports = 0;
volatile LONG g_prePollX = 0;
volatile LONG g_prePollY = 0;
volatile LONG g_droppedPackets = 0;
volatile LONG g_reportCount = 0;
volatile LONG g_pollCount = 0;
volatile LONG g_intervalX = 0;
volatile LONG g_intervalY = 0;
alignas(8) volatile LONG64 g_oldestMovementTick = 0;
alignas(8) volatile LONG64 g_newestMovementTick = 0;
volatile LONG g_pendingMovementReports = 0;
volatile LONG g_latencySamples = 0;
alignas(8) volatile LONG64 g_totalOldestAgeUs = 0;
alignas(8) volatile LONG64 g_totalNewestAgeUs = 0;
alignas(8) volatile LONG64 g_maxOldestAgeUs = 0;
volatile LONG g_maxReportsPerPoll = 0;
volatile LONG g_emptyPolls = 0;
alignas(8) volatile LONG64 g_lastPollTick = 0;
alignas(8) volatile LONG64 g_totalPollIntervalUs = 0;
alignas(8) volatile LONG64 g_maxPollIntervalUs = 0;
volatile LONG g_pollIntervals = 0;
LARGE_INTEGER g_counterFrequency = {};
volatile LONG g_absoluteReports = 0;
volatile LONG g_sizeFailures = 0;
volatile LONG g_readFailures = 0;
bool g_enabled = false;
DWORD g_statisticsIntervalMs = 5000;
DWORD g_lastStatisticsTick = 0;
HWND g_window = nullptr;
PollMouseInputFn g_legacyPoll = nullptr;
HANDLE g_loggedDevices[16] = {};
size_t g_loggedDeviceCount = 0;

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

void UpdateMaximum(volatile LONG* destination, LONG value) {
    LONG observed = InterlockedCompareExchange(destination, 0, 0);
    while (value > observed) {
        const LONG previous = InterlockedCompareExchange(destination, value, observed);
        if (previous == observed) break;
        observed = previous;
    }
}

bool WindowHasInputFocus(HWND* focusedWindow = nullptr) {
    const HWND focus = GetFocus();
    if (focusedWindow != nullptr) *focusedWindow = focus;
    return focus == g_window ||
           (focus != nullptr && GetAncestor(focus, GA_ROOT) == g_window);
}

WPARAM CurrentState() {
    WPARAM state = static_cast<WPARAM>(InterlockedCompareExchange(&g_buttonState, 0, 0));
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) state |= MK_SHIFT;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) state |= MK_CONTROL;
    return state;
}

LPARAM CurrentPosition() {
    const LONG x = *reinterpret_cast<volatile LONG*>(kCursorX);
    const LONG y = *reinterpret_cast<volatile LONG*>(kCursorY);
    return MAKELPARAM(static_cast<short>(x), static_cast<short>(y));
}

void UpdateVirtualCursor(LONG deltaX, LONG deltaY) {
    const LONG width = *reinterpret_cast<volatile LONG*>(kScreenWidth);
    const LONG height = *reinterpret_cast<volatile LONG*>(kScreenHeight);
    LONG x = *reinterpret_cast<volatile LONG*>(kCursorX) + deltaX;
    LONG y = *reinterpret_cast<volatile LONG*>(kCursorY) + deltaY;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (width > 0 && x >= width) x = width - 1;
    if (height > 0 && y >= height) y = height - 1;
    *reinterpret_cast<volatile LONG*>(kCursorX) = x;
    *reinterpret_cast<volatile LONG*>(kCursorY) = y;
}

void DispatchEvent(UINT message, WPARAM state, LPARAM position) {
    const auto dispatch = reinterpret_cast<MouseDispatcherFn>(kMouseDispatcher);
    dispatch(state, position, message, 1);
    logger::Log("INFO", "RawInput.Dispatch",
                "message=0x%04X state=0x%04lX x=%d y=%d",
                message, static_cast<unsigned long>(state),
                static_cast<int>(static_cast<short>(LOWORD(position))),
                static_cast<int>(static_cast<short>(HIWORD(position))));
}

void ClearInputState() {
    InterlockedExchange(&g_accumX, 0);
    InterlockedExchange(&g_accumY, 0);
    InterlockedExchange(&g_buttonState, 0);
    InterlockedExchange(&g_pendingMovementReports, 0);
    InterlockedExchange64(&g_oldestMovementTick, 0);
    InterlockedExchange64(&g_newestMovementTick, 0);
}

void ResetMovementStatistics() {
    InterlockedExchange(&g_reportCount, 0);
    InterlockedExchange(&g_pollCount, 0);
    InterlockedExchange(&g_intervalX, 0);
    InterlockedExchange(&g_intervalY, 0);
    InterlockedExchange(&g_latencySamples, 0);
    InterlockedExchange64(&g_totalOldestAgeUs, 0);
    InterlockedExchange64(&g_totalNewestAgeUs, 0);
    InterlockedExchange64(&g_maxOldestAgeUs, 0);
    InterlockedExchange(&g_maxReportsPerPoll, 0);
    InterlockedExchange(&g_emptyPolls, 0);
    InterlockedExchange64(&g_lastPollTick, 0);
    InterlockedExchange64(&g_totalPollIntervalUs, 0);
    InterlockedExchange64(&g_maxPollIntervalUs, 0);
    InterlockedExchange(&g_pollIntervals, 0);
    g_lastStatisticsTick = GetTickCount();
}

void SetButton(USHORT flags, USHORT downFlag, USHORT upFlag, LONG stateBit,
               UINT downMessage, UINT upMessage) {
    if ((flags & downFlag) != 0) {
        LONG oldState = InterlockedCompareExchange(&g_buttonState, 0, 0);
        LONG newState = 0;
        do {
            newState = oldState | stateBit;
            const LONG observed = InterlockedCompareExchange(&g_buttonState, newState, oldState);
            if (observed == oldState) break;
            oldState = observed;
        } while (true);
        const WPARAM state = static_cast<WPARAM>(newState);
        DispatchEvent(downMessage, state | (CurrentState() & (MK_SHIFT | MK_CONTROL)),
                      CurrentPosition());
        logger::Log("INFO", "RawInput.Button", "message=0x%04X state=0x%04lX", downMessage,
                    static_cast<unsigned long>(state));
    }
    if ((flags & upFlag) != 0) {
        LONG oldState = InterlockedCompareExchange(&g_buttonState, 0, 0);
        LONG newState = 0;
        do {
            newState = oldState & ~stateBit;
            const LONG observed = InterlockedCompareExchange(&g_buttonState, newState, oldState);
            if (observed == oldState) break;
            oldState = observed;
        } while (true);
        const WPARAM state = static_cast<WPARAM>(newState);
        DispatchEvent(upMessage, state | (CurrentState() & (MK_SHIFT | MK_CONTROL)),
                      CurrentPosition());
        logger::Log("INFO", "RawInput.Button", "message=0x%04X state=0x%04lX", upMessage,
                    static_cast<unsigned long>(state));
    }
}

void LogDevice(HANDLE device) {
    for (size_t i = 0; i < g_loggedDeviceCount; ++i) {
        if (g_loggedDevices[i] == device) return;
    }
    if (g_loggedDeviceCount < sizeof(g_loggedDevices) / sizeof(g_loggedDevices[0])) {
        g_loggedDevices[g_loggedDeviceCount++] = device;
    }

    RID_DEVICE_INFO info = {};
    info.cbSize = sizeof(info);
    UINT infoSize = sizeof(info);
    const UINT infoResult = GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, &info, &infoSize);
    wchar_t wideName[512] = {};
    UINT nameSize = sizeof(wideName) / sizeof(wideName[0]);
    const UINT nameResult = GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, wideName, &nameSize);
    char name[1024] = "unavailable";
    if (nameResult != static_cast<UINT>(-1)) {
        WideCharToMultiByte(CP_UTF8, 0, wideName, -1, name, sizeof(name), nullptr, nullptr);
    }
    if (infoResult != static_cast<UINT>(-1) && info.dwType == RIM_TYPEMOUSE) {
        logger::Log("INFO", "RawInput.Device",
                    "handle=0x%08lX name=%s buttons=%lu sample_rate=%lu horizontal_wheel=%d",
                    reinterpret_cast<unsigned long>(device), name, info.mouse.dwNumberOfButtons,
                    info.mouse.dwSampleRate, info.mouse.fHasHorizontalWheel);
    } else {
        logger::Log("WARN", "RawInput.Device", "handle=0x%08lX name=%s device_info_error=%lu",
                    reinterpret_cast<unsigned long>(device), name, GetLastError());
    }
}

void ProcessRawInput(HRAWINPUT handle) {
    UINT size = 0;
    if (GetRawInputData(handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 ||
        size == 0) {
        InterlockedIncrement(&g_droppedPackets);
        InterlockedIncrement(&g_sizeFailures);
        logger::Log("ERROR", "RawInput", "GetRawInputData size query failed: error=%lu", GetLastError());
        return;
    }

    unsigned char stackBuffer[sizeof(RAWINPUT) + 64] = {};
    unsigned char* buffer = stackBuffer;
    if (size > sizeof(stackBuffer)) {
        buffer = static_cast<unsigned char*>(HeapAlloc(GetProcessHeap(), 0, size));
        if (buffer == nullptr) return;
    }

    const UINT read = GetRawInputData(handle, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER));
    if (read == size) {
        const RAWINPUT* input = reinterpret_cast<const RAWINPUT*>(buffer);
        if (input->header.dwType == RIM_TYPEMOUSE &&
            InterlockedCompareExchange(&g_backendState, kInactive, kInactive) == kActive) {
            LogDevice(input->header.hDevice);
            InterlockedIncrement(&g_reportCount);
            const RAWMOUSE& mouse = input->data.mouse;
            if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                if (InterlockedCompareExchange(&g_awaitingFirstPoll, 0, 0) != 0) {
                    InterlockedIncrement(&g_prePollReports);
                    InterlockedExchangeAdd(&g_prePollX, mouse.lLastX);
                    InterlockedExchangeAdd(&g_prePollY, mouse.lLastY);
                    // Menus still need a moving virtual cursor even though
                    // these deltas must never reach the first gameplay tick.
                    UpdateVirtualCursor(mouse.lLastX, mouse.lLastY);
                } else {
                    const LONG64 received = CounterNow();
                    InterlockedCompareExchange64(&g_oldestMovementTick, received, 0);
                    InterlockedExchange64(&g_newestMovementTick, received);
                    InterlockedIncrement(&g_pendingMovementReports);
                    InterlockedExchangeAdd(&g_accumX, mouse.lLastX);
                    InterlockedExchangeAdd(&g_accumY, mouse.lLastY);
                    UpdateVirtualCursor(mouse.lLastX, mouse.lLastY);
                }
            } else {
                InterlockedIncrement(&g_absoluteReports);
            }

            SetButton(mouse.usButtonFlags, RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP,
                      MK_LBUTTON, WM_LBUTTONDOWN, WM_LBUTTONUP);
            SetButton(mouse.usButtonFlags, RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP,
                      MK_RBUTTON, WM_RBUTTONDOWN, WM_RBUTTONUP);
            SetButton(mouse.usButtonFlags, RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP,
                      MK_MBUTTON, WM_MBUTTONDOWN, WM_MBUTTONUP);
            if ((mouse.usButtonFlags & RI_MOUSE_WHEEL) != 0) {
                const WPARAM wheel = MAKEWPARAM(CurrentState(), static_cast<USHORT>(mouse.usButtonData));
                DispatchEvent(WM_MOUSEWHEEL, wheel, CurrentPosition());
                logger::Log("INFO", "RawInput.Wheel", "delta=%d state=0x%04lX",
                            static_cast<short>(mouse.usButtonData),
                            static_cast<unsigned long>(CurrentState()));
            }
        }
    } else {
        InterlockedIncrement(&g_droppedPackets);
        InterlockedIncrement(&g_readFailures);
        logger::Log("ERROR", "RawInput", "GetRawInputData read failed: expected=%u actual=%u error=%lu",
                    size, read, GetLastError());
    }

    if (buffer != stackBuffer) HeapFree(GetProcessHeap(), 0, buffer);
}

void SuspendInput() {
    if (InterlockedExchange(&g_backendState, kInactive) == kInactive) return;
    ClearInputState();
    logger::Log("INFO", "RawInput", "input suspended and transient state cleared");
}

bool RegisterForWindow(HWND window);

bool ResumeInput(const char* trigger, bool clipReady) {
    HWND focusedWindow = nullptr;
    const bool hasInputFocus = WindowHasInputFocus(&focusedWindow);
    if (!clipReady || g_window == nullptr || !IsWindow(g_window) ||
        !IsWindowVisible(g_window) || IsIconic(g_window) ||
        GetForegroundWindow() != g_window || !hasInputFocus) {
        InterlockedExchange(&g_backendState, kRecoveryPending);
        DWORD focusProcess = 0;
        const DWORD focusThread = focusedWindow == nullptr
            ? 0 : GetWindowThreadProcessId(focusedWindow, &focusProcess);
        logger::Log("INFO", "RawInput",
                    "resume=deferred trigger=%s clip_ready=%d foreground=%d focus=%d "
                    "focus_hwnd=0x%08lX focus_thread=%lu focus_process=%lu visible=%d iconic=%d",
                    trigger, clipReady, GetForegroundWindow() == g_window, hasInputFocus,
                    reinterpret_cast<unsigned long>(focusedWindow), focusThread, focusProcess,
                    g_window && IsWindowVisible(g_window), g_window && IsIconic(g_window));
        return false;
    }
    if (InterlockedCompareExchange(&g_registered, 0, 0) == 0 &&
        !RegisterForWindow(g_window)) {
        InterlockedExchange(&g_backendState, kRecoveryPending);
        logger::Log("ERROR", "RawInput", "resume=deferred trigger=%s registration_failed=1",
                    trigger);
        return false;
    }
    if (InterlockedCompareExchange(&g_backendState, kInactive, kInactive) == kActive) {
        return true;
    }
    ClearInputState();
    InterlockedExchange(&g_awaitingFirstPoll, 1);
    InterlockedExchange(&g_prePollReports, 0);
    InterlockedExchange(&g_prePollX, 0);
    InterlockedExchange(&g_prePollY, 0);
    InterlockedExchange(&g_backendState, kActive);
    const LONG activatedBefore = InterlockedExchange(&g_activatedOnce, 1);
    logger::Log("INFO", "RawInput", "resume=completed trigger=%s", trigger);
    if (activatedBefore == 0 &&
        InterlockedCompareExchange(&g_startupLegacyFallbackLogged, 0, 0) != 0) {
        logger::Log("INFO", "RawInput", "startup fallback=raw_input");
    }
    return true;
}

bool RegisterForWindow(HWND window) {
    if (!g_enabled) return true;
    if (g_window == window && InterlockedCompareExchange(&g_registered, 0, 0)) return true;
    if (window == nullptr || !IsWindow(window)) return false;
    g_window = window;
    RAWINPUTDEVICE device = {0x01, 0x02, RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE, window};
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
        logger::Log("ERROR", "RawInput",
                    "RegisterRawInputDevices flags=NOLEGACY|CAPTUREMOUSE failed: error=%lu",
                    GetLastError());
        return false;
    }
    InterlockedExchange(&g_registered, 1);
    InterlockedExchange(&g_backendState, kRecoveryPending);
    logger::Log("INFO", "RawInput", "mouse registered; state=recovery_pending");
    return true;
}

void UnregisterRawInput() {
    if (InterlockedExchange(&g_registered, 0) == 0) return;
    RAWINPUTDEVICE device = {0x01, 0x02, RIDEV_REMOVE, nullptr};
    SetLastError(0);
    const BOOL result = RegisterRawInputDevices(&device, 1, sizeof(device));
    logger::Log(result ? "INFO" : "ERROR", "RawInput",
                "unregister result=%d error=%lu", result,
                result ? ERROR_SUCCESS : GetLastError());
}

void EnsureInitialized() {
    if (g_window != nullptr || InterlockedCompareExchange(&g_initializing, 1, 0) != 0) return;
    const HWND window = *reinterpret_cast<HWND*>(kGameWindow);
    if (!game_window::EnsureInstalled(window)) InterlockedExchange(&g_initializing, 0);
}

extern "C" void __cdecl RawPollMouseInput() {
    EnsureInitialized();
    if (InterlockedCompareExchange(&g_backendState, kInactive, kInactive) != kActive) {
        if (g_window == nullptr ||
            InterlockedCompareExchange(&g_activatedOnce, 0, 0) == 0) {
            if (g_window != nullptr &&
                InterlockedCompareExchange(&g_startupLegacyFallbackLogged, 1, 0) == 0) {
                logger::Log("INFO", "RawInput", "startup fallback=legacy");
            }
            g_legacyPoll();
        } else {
            *reinterpret_cast<volatile LONG*>(kRelativeX) = 0;
            *reinterpret_cast<volatile LONG*>(kRelativeY) = 0;
        }
        return;
    }

    const LONG64 pollTick = CounterNow();
    frame_pacing::NotifyInputPoll(pollTick);
    if (InterlockedExchange(&g_awaitingFirstPoll, 0) != 0) {
        const LONG discardedReports = InterlockedExchange(&g_prePollReports, 0);
        const LONG discardedX = InterlockedExchange(&g_prePollX, 0);
        const LONG discardedY = InterlockedExchange(&g_prePollY, 0);
        ClearInputState();
        ResetMovementStatistics();
        *reinterpret_cast<volatile LONG*>(kRelativeX) = 0;
        *reinterpret_cast<volatile LONG*>(kRelativeY) = 0;
        logger::Log("INFO", "RawInput",
                    "first_poll_ready pre_poll_reports_discarded=%ld pre_poll_dx=%ld pre_poll_dy=%ld",
                    discardedReports, discardedX, discardedY);
        cursor_clip::HandleWindowChanged("first_raw_poll");
        return;
    }
    const LONG64 previousPoll = InterlockedExchange64(&g_lastPollTick, pollTick);
    if (previousPoll != 0) {
        const LONG64 intervalUs = TicksToMicroseconds(pollTick - previousPoll);
        InterlockedExchangeAdd64(&g_totalPollIntervalUs, intervalUs);
        UpdateMaximum(&g_maxPollIntervalUs, intervalUs);
        InterlockedIncrement(&g_pollIntervals);
    }
    *reinterpret_cast<volatile LONG*>(kMouseState) = static_cast<LONG>(CurrentState());
    const LONG x = InterlockedExchange(&g_accumX, 0);
    const LONG y = InterlockedExchange(&g_accumY, 0);
    const LONG reportsInPoll = InterlockedExchange(&g_pendingMovementReports, 0);
    const LONG64 oldestTick = InterlockedExchange64(&g_oldestMovementTick, 0);
    const LONG64 newestTick = InterlockedExchange64(&g_newestMovementTick, 0);
    if (reportsInPoll > 0 && oldestTick != 0 && newestTick != 0) {
        const LONG64 oldestAgeUs = TicksToMicroseconds(pollTick - oldestTick);
        const LONG64 newestAgeUs = TicksToMicroseconds(pollTick - newestTick);
        InterlockedExchangeAdd64(&g_totalOldestAgeUs, oldestAgeUs);
        InterlockedExchangeAdd64(&g_totalNewestAgeUs, newestAgeUs);
        UpdateMaximum(&g_maxOldestAgeUs, oldestAgeUs);
        UpdateMaximum(&g_maxReportsPerPoll, reportsInPoll);
        InterlockedIncrement(&g_latencySamples);
    } else {
        InterlockedIncrement(&g_emptyPolls);
    }
    InterlockedExchangeAdd(&g_intervalX, x);
    InterlockedExchangeAdd(&g_intervalY, y);
    *reinterpret_cast<volatile LONG*>(kRelativeX) = x;
    *reinterpret_cast<volatile LONG*>(kRelativeY) = y;
    InterlockedIncrement(&g_pollCount);
    const DWORD now = GetTickCount();
    if (now - g_lastStatisticsTick >= g_statisticsIntervalMs) {
        const LONG reports = InterlockedExchange(&g_reportCount, 0);
        const LONG polls = InterlockedExchange(&g_pollCount, 0);
        const LONG totalX = InterlockedExchange(&g_intervalX, 0);
        const LONG totalY = InterlockedExchange(&g_intervalY, 0);
        const LONG dropped = InterlockedExchange(&g_droppedPackets, 0);
        const LONG absolute = InterlockedExchange(&g_absoluteReports, 0);
        const LONG sizeFailures = InterlockedExchange(&g_sizeFailures, 0);
        const LONG readFailures = InterlockedExchange(&g_readFailures, 0);
        const LONG latencySamples = InterlockedExchange(&g_latencySamples, 0);
        const LONG64 totalOldestAgeUs = InterlockedExchange64(&g_totalOldestAgeUs, 0);
        const LONG64 totalNewestAgeUs = InterlockedExchange64(&g_totalNewestAgeUs, 0);
        const LONG64 maxOldestAgeUs = InterlockedExchange64(&g_maxOldestAgeUs, 0);
        const LONG maxReportsPerPoll = InterlockedExchange(&g_maxReportsPerPoll, 0);
        const LONG emptyPolls = InterlockedExchange(&g_emptyPolls, 0);
        const LONG pollIntervals = InterlockedExchange(&g_pollIntervals, 0);
        const LONG64 totalPollIntervalUs = InterlockedExchange64(&g_totalPollIntervalUs, 0);
        const LONG64 maxPollIntervalUs = InterlockedExchange64(&g_maxPollIntervalUs, 0);
        g_lastStatisticsTick = now;
        logger::Log("INFO", "RawInput.Stats",
                    "interval_ms=%lu reports=%ld polls=%ld total_dx=%ld total_dy=%ld dropped=%ld "
                    "absolute_ignored=%ld size_failures=%ld read_failures=%ld active=%ld",
                    g_statisticsIntervalMs, reports, polls, totalX, totalY, dropped, absolute,
                    sizeFailures, readFailures,
                    InterlockedCompareExchange(&g_backendState, kInactive, kInactive) == kActive);
        logger::Log("INFO", "RawInput.Latency",
                    "samples=%ld oldest_avg_us=%lld newest_avg_us=%lld oldest_max_us=%lld "
                    "reports_per_poll_max=%ld empty_polls=%ld poll_interval_avg_us=%lld "
                    "poll_interval_max_us=%lld qpc_frequency=%lld",
                    latencySamples,
                    latencySamples ? totalOldestAgeUs / latencySamples : 0,
                    latencySamples ? totalNewestAgeUs / latencySamples : 0,
                    maxOldestAgeUs, maxReportsPerPoll, emptyPolls,
                    pollIntervals ? totalPollIntervalUs / pollIntervals : 0,
                    maxPollIntervalUs, g_counterFrequency.QuadPart);
        game_window::HandleStatisticsInterval();
        mouse_scaling_fix::LogStatistics();
    }
}

bool WriteDetour() {
    auto* target = reinterpret_cast<unsigned char*>(kPollMouseInput);
    if (std::memcmp(target, kExpectedPollBytes, kDetourSize) != 0) {
        logger::Log("ERROR", "RawInput", "unexpected PollMouseInput bytes at 0x%08lX",
                    static_cast<unsigned long>(kPollMouseInput));
        return false;
    }

    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, kDetourSize + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        logger::Log("ERROR", "RawInput", "VirtualAlloc trampoline failed: error=%lu", GetLastError());
        return false;
    }
    std::memcpy(trampoline, target, kDetourSize);
    trampoline[kDetourSize] = 0xE9;
    *reinterpret_cast<int32_t*>(trampoline + kDetourSize + 1) =
        static_cast<int32_t>((kPollMouseInput + kDetourSize) -
                             reinterpret_cast<uintptr_t>(trampoline + kDetourSize + 5));
    g_legacyPoll = reinterpret_cast<PollMouseInputFn>(trampoline);

    unsigned char detour[kDetourSize] = {0xE9, 0, 0, 0, 0, 0x90};
    *reinterpret_cast<int32_t*>(detour + 1) = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(&RawPollMouseInput) - (kPollMouseInput + 5));
    DWORD oldProtect = 0;
    if (!VirtualProtect(target, sizeof(detour), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        logger::Log("ERROR", "RawInput", "VirtualProtect detour failed: error=%lu", GetLastError());
        return false;
    }
    std::memcpy(target, detour, sizeof(detour));
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(detour));
    DWORD ignored = 0;
    VirtualProtect(target, sizeof(detour), oldProtect, &ignored);
    logger::Log("INFO", "RawInput", "PollMouseInput detour installed; trampoline=0x%08lX",
                reinterpret_cast<unsigned long>(trampoline));
    return true;
}
}  // namespace

bool Install(const Settings& settings) {
    if (!settings.enabled) {
        logger::Log("INFO", "RawInput", "feature disabled");
        return true;
    }
    logger::Log("INFO", "RawInput", "feature enabled");
    g_enabled = true;
    g_statisticsIntervalMs = settings.statisticsIntervalMs;
    QueryPerformanceFrequency(&g_counterFrequency);
    g_lastStatisticsTick = GetTickCount();
    if (!WriteDetour()) {
        g_enabled = false;
        logger::Log("ERROR", "RawInput", "installation failed; legacy input retained");
        return false;
    }
    return true;
}

bool AttachWindow(HWND window) { return RegisterForWindow(window); }
void HandleRawInput(HRAWINPUT input) { ProcessRawInput(input); }
void HandleFocusLost() {
    if (!g_enabled) return;
    SuspendInput();
    UnregisterRawInput();
}
bool HandleFocusGained(const char* trigger, bool clipReady) {
    return !g_enabled || ResumeInput(trigger, clipReady);
}
void HandleDestroy() {
    if (!g_enabled) return;
    SuspendInput();
    UnregisterRawInput();
    g_window = nullptr;
}
bool IsEnabled() { return g_enabled; }
}  // namespace raw_input
