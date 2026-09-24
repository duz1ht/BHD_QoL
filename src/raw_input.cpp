#include "raw_input.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game_window.h"
#include "logger.h"

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

volatile LONG g_logicAccumX = 0;
volatile LONG g_logicAccumY = 0;
volatile LONG g_renderAccumX = 0;
volatile LONG g_renderAccumY = 0;
volatile LONG g_lastRenderDeltaX = 0;
volatile LONG g_lastRenderDeltaY = 0;
volatile LONG g_buttonState = 0;
enum BackendState : LONG { kInactive = 0, kRecoveryPending = 1, kActive = 2 };
volatile LONG g_backendState = kInactive;
volatile LONG g_registered = 0;
volatile LONG g_initializing = 0;
volatile LONG g_activatedOnce = 0;
volatile LONG g_startupLegacyFallbackLogged = 0;
volatile LONG g_dropNextMovement = 0;
volatile LONG g_droppedPackets = 0;
volatile LONG g_reportCount = 0;
volatile LONG g_logicPollCount = 0;
volatile LONG g_renderPollCount = 0;
volatile LONG g_rawIntervalX = 0;
volatile LONG g_rawIntervalY = 0;
volatile LONG g_logicIntervalX = 0;
volatile LONG g_logicIntervalY = 0;
volatile LONG g_renderIntervalX = 0;
volatile LONG g_renderIntervalY = 0;
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
thread_local bool g_renderPollContext = false;

class RenderPollScope {
public:
    RenderPollScope() : previous_(g_renderPollContext) { g_renderPollContext = true; }
    ~RenderPollScope() { g_renderPollContext = previous_; }

private:
    bool previous_;
};

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
    InterlockedExchange(&g_logicAccumX, 0);
    InterlockedExchange(&g_logicAccumY, 0);
    InterlockedExchange(&g_renderAccumX, 0);
    InterlockedExchange(&g_renderAccumY, 0);
    InterlockedExchange(&g_lastRenderDeltaX, 0);
    InterlockedExchange(&g_lastRenderDeltaY, 0);
    InterlockedExchange(&g_buttonState, 0);
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
                if (InterlockedExchange(&g_dropNextMovement, 0) == 0) {
                    InterlockedExchangeAdd(&g_logicAccumX, mouse.lLastX);
                    InterlockedExchangeAdd(&g_logicAccumY, mouse.lLastY);
                    InterlockedExchangeAdd(&g_renderAccumX, mouse.lLastX);
                    InterlockedExchangeAdd(&g_renderAccumY, mouse.lLastY);
                    InterlockedExchangeAdd(&g_rawIntervalX, mouse.lLastX);
                    InterlockedExchangeAdd(&g_rawIntervalY, mouse.lLastY);
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
    InterlockedExchange(&g_dropNextMovement, 1);
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

    *reinterpret_cast<volatile LONG*>(kMouseState) = static_cast<LONG>(CurrentState());
    LONG x = 0;
    LONG y = 0;
    if (g_renderPollContext) {
        x = InterlockedExchange(&g_renderAccumX, 0);
        y = InterlockedExchange(&g_renderAccumY, 0);
        InterlockedExchange(&g_lastRenderDeltaX, x);
        InterlockedExchange(&g_lastRenderDeltaY, y);
        InterlockedExchangeAdd(&g_renderIntervalX, x);
        InterlockedExchangeAdd(&g_renderIntervalY, y);
        InterlockedIncrement(&g_renderPollCount);
    } else {
        x = InterlockedExchange(&g_logicAccumX, 0);
        y = InterlockedExchange(&g_logicAccumY, 0);
        InterlockedExchangeAdd(&g_logicIntervalX, x);
        InterlockedExchangeAdd(&g_logicIntervalY, y);
        InterlockedIncrement(&g_logicPollCount);
    }
    *reinterpret_cast<volatile LONG*>(kRelativeX) = x;
    *reinterpret_cast<volatile LONG*>(kRelativeY) = y;
    const DWORD now = GetTickCount();
    if (now - g_lastStatisticsTick >= g_statisticsIntervalMs) {
        const LONG reports = InterlockedExchange(&g_reportCount, 0);
        const LONG logicPolls = InterlockedExchange(&g_logicPollCount, 0);
        const LONG renderPolls = InterlockedExchange(&g_renderPollCount, 0);
        const LONG rawX = InterlockedExchange(&g_rawIntervalX, 0);
        const LONG rawY = InterlockedExchange(&g_rawIntervalY, 0);
        const LONG logicX = InterlockedExchange(&g_logicIntervalX, 0);
        const LONG logicY = InterlockedExchange(&g_logicIntervalY, 0);
        const LONG renderX = InterlockedExchange(&g_renderIntervalX, 0);
        const LONG renderY = InterlockedExchange(&g_renderIntervalY, 0);
        const LONG dropped = InterlockedExchange(&g_droppedPackets, 0);
        const LONG absolute = InterlockedExchange(&g_absoluteReports, 0);
        const LONG sizeFailures = InterlockedExchange(&g_sizeFailures, 0);
        const LONG readFailures = InterlockedExchange(&g_readFailures, 0);
        g_lastStatisticsTick = now;
        logger::Log("INFO", "RawInput.Stats",
                    "interval_ms=%lu reports=%ld logic_polls=%ld render_polls=%ld "
                    "raw_dx=%ld raw_dy=%ld logic_dx=%ld logic_dy=%ld "
                    "render_dx=%ld render_dy=%ld dropped=%ld "
                    "absolute_ignored=%ld size_failures=%ld read_failures=%ld active=%ld",
                    g_statisticsIntervalMs, reports, logicPolls, renderPolls, rawX, rawY,
                    logicX, logicY, renderX, renderY, dropped, absolute,
                    sizeFailures, readFailures,
                    InterlockedCompareExchange(&g_backendState, kInactive, kInactive) == kActive);
        game_window::HandleStatisticsInterval();
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
bool IsBackendActive() {
    return g_enabled &&
           InterlockedCompareExchange(&g_backendState, kInactive, kInactive) == kActive;
}
void PollForRenderFrame() {
    if (!IsBackendActive()) return;
    RenderPollScope scope;
    reinterpret_cast<PollMouseInputFn>(kPollMouseInput)();
}
RenderMouseDelta GetLastRenderDelta() {
    return {InterlockedCompareExchange(&g_lastRenderDeltaX, 0, 0),
            InterlockedCompareExchange(&g_lastRenderDeltaY, 0, 0)};
}
}  // namespace raw_input
