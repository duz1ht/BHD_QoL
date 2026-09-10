#include "raw_input.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

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
constexpr size_t kEventCapacity = 128;

using PollMouseInputFn = void(__cdecl*)();
using MouseDispatcherFn = void(__cdecl*)(WPARAM, LPARAM, UINT, int);

struct MouseEvent {
    WPARAM state;
    LPARAM position;
    UINT message;
};

volatile LONG g_accumX = 0;
volatile LONG g_accumY = 0;
volatile LONG g_buttonState = 0;
enum BackendState : LONG { kInactive = 0, kRecoveryPending = 1, kActive = 2 };
volatile LONG g_backendState = kInactive;
volatile LONG g_registered = 0;
volatile LONG g_initializing = 0;
volatile LONG g_dropNextMovement = 0;
volatile LONG g_droppedPackets = 0;
volatile LONG g_reportCount = 0;
volatile LONG g_pollCount = 0;
volatile LONG g_intervalX = 0;
volatile LONG g_intervalY = 0;
volatile LONG g_absoluteReports = 0;
volatile LONG g_sizeFailures = 0;
volatile LONG g_readFailures = 0;
volatile LONG g_queueOverflows = 0;
DWORD g_statisticsIntervalMs = 5000;
DWORD g_lastStatisticsTick = 0;
HWND g_window = nullptr;
WNDPROC g_originalWndProc = nullptr;
PollMouseInputFn g_legacyPoll = nullptr;
CRITICAL_SECTION g_eventLock;
MouseEvent g_events[kEventCapacity] = {};
size_t g_eventRead = 0;
size_t g_eventWrite = 0;
RECT g_lastValidGameClip = {};
bool g_hasLastValidGameClip = false;
LONG g_lastOnOtherMonitor = -1;
HANDLE g_loggedDevices[16] = {};
size_t g_loggedDeviceCount = 0;

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

void QueueEvent(UINT message, WPARAM state, LPARAM position) {
    EnterCriticalSection(&g_eventLock);
    const size_t next = (g_eventWrite + 1) % kEventCapacity;
    if (next == g_eventRead) {
        g_eventRead = (g_eventRead + 1) % kEventCapacity;
        InterlockedIncrement(&g_droppedPackets);
        InterlockedIncrement(&g_queueOverflows);
        logger::Log("WARN", "RawInput.Queue", "queue full; oldest event discarded");
    }
    g_events[g_eventWrite] = {state, position, message};
    g_eventWrite = next;
    LeaveCriticalSection(&g_eventLock);
}

void ClearInputState() {
    InterlockedExchange(&g_accumX, 0);
    InterlockedExchange(&g_accumY, 0);
    InterlockedExchange(&g_buttonState, 0);
    EnterCriticalSection(&g_eventLock);
    g_eventRead = g_eventWrite;
    LeaveCriticalSection(&g_eventLock);
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
        QueueEvent(downMessage, state | (CurrentState() & (MK_SHIFT | MK_CONTROL)), CurrentPosition());
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
        QueueEvent(upMessage, state | (CurrentState() & (MK_SHIFT | MK_CONTROL)), CurrentPosition());
        logger::Log("INFO", "RawInput.Button", "message=0x%04X state=0x%04lX", upMessage,
                    static_cast<unsigned long>(state));
    }
}

bool GetClientScreenRect(RECT* result) {
    if (result == nullptr || g_window == nullptr || !IsWindow(g_window)) return false;
    RECT client = {};
    if (!GetClientRect(g_window, &client)) return false;
    POINT upperLeft = {client.left, client.top};
    POINT lowerRight = {client.right, client.bottom};
    if (!ClientToScreen(g_window, &upperLeft) || !ClientToScreen(g_window, &lowerRight)) return false;
    *result = {upperLeft.x, upperLeft.y, lowerRight.x, lowerRight.y};
    return !IsRectEmpty(result);
}

bool ClipIsContainedBy(const RECT& clip, const RECT& bounds) {
    return !IsRectEmpty(&clip) && clip.left >= bounds.left && clip.top >= bounds.top &&
           clip.right <= bounds.right && clip.bottom <= bounds.bottom;
}

bool WindowReadyForInput() {
    return g_window != nullptr && IsWindow(g_window) && IsWindowVisible(g_window) &&
           !IsIconic(g_window) && GetForegroundWindow() == g_window && GetFocus() == g_window;
}

void LogCursorSnapshot(const char* trigger) {
    if (g_window == nullptr || !IsWindow(g_window)) {
        logger::Log("WARN", "CursorClip", "trigger=%s status=window_invalid", trigger);
        return;
    }

    RECT clientScreen = {};
    const bool converted = GetClientScreenRect(&clientScreen);
    RECT clip = {};
    POINT cursor = {};
    const bool clipValid = GetClipCursor(&clip) != FALSE;
    const bool cursorValid = GetCursorPos(&cursor) != FALSE;
    RECT desktop = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                    GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                    GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};

    const char* status = "clip_query_failed";
    const char* canEscape = "unknown";
    if (clipValid && converted) {
        const bool containedByClient = ClipIsContainedBy(clip, clientScreen);
        if (EqualRect(&clip, &desktop)) {
            status = "not_confined";
            canEscape = "yes";
        } else if (containedByClient) {
            const bool fullClient = std::abs(clip.left - clientScreen.left) <= 1 &&
                                    std::abs(clip.top - clientScreen.top) <= 1 &&
                                    std::abs(clip.right - clientScreen.right) <= 1 &&
                                    std::abs(clip.bottom - clientScreen.bottom) <= 1;
            status = fullClient ? "confined_to_full_client" : "confined_inside_client";
            canEscape = "no";
            if (!IsIconic(g_window)) {
                g_lastValidGameClip = clip;
                g_hasLastValidGameClip = true;
            }
        } else {
            status = "confined_to_other_rect";
            canEscape = "yes";
        }
    }
    const bool focused = GetForegroundWindow() == g_window && GetFocus() == g_window;
    const bool outsideClient = cursorValid && converted && !PtInRect(&clientScreen, cursor);
    const HMONITOR gameMonitor = MonitorFromWindow(g_window, MONITOR_DEFAULTTONULL);
    const HMONITOR cursorMonitor = cursorValid ? MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL) : nullptr;
    MONITORINFOEXA gameInfo = {};
    MONITORINFOEXA cursorInfo = {};
    gameInfo.cbSize = sizeof(gameInfo);
    cursorInfo.cbSize = sizeof(cursorInfo);
    const bool gameMonitorValid =
        gameMonitor != nullptr &&
        GetMonitorInfoA(gameMonitor, reinterpret_cast<MONITORINFO*>(&gameInfo));
    const bool cursorMonitorValid =
        cursorMonitor != nullptr &&
        GetMonitorInfoA(cursorMonitor, reinterpret_cast<MONITORINFO*>(&cursorInfo));
    const LONG onOtherMonitor = gameMonitorValid && cursorMonitorValid
                                    ? (gameMonitor != cursorMonitor ? 1 : 0)
                                    : -1;
    const bool monitorTransition = onOtherMonitor == 1 && g_lastOnOtherMonitor != 1;
    g_lastOnOtherMonitor = onOtherMonitor;
    const LONG internalWidth = *reinterpret_cast<volatile LONG*>(kScreenWidth);
    const LONG internalHeight = *reinterpret_cast<volatile LONG*>(kScreenHeight);
    logger::Log((outsideClient && focused) || monitorTransition ? "WARN" : "INFO", "CursorClip",
                "trigger=%s status=%s can_escape=%s client=(%ld,%ld)-(%ld,%ld) clip=(%ld,%ld)-(%ld,%ld) "
                "desktop=(%ld,%ld)-(%ld,%ld) cursor=(%ld,%ld) outside_client=%d foreground=%d "
                "focus=%d visible=%d iconic=%d internal_resolution=%ldx%ld "
                "game_monitor=%s game_monitor_rect=(%ld,%ld)-(%ld,%ld) "
                "game_work=(%ld,%ld)-(%ld,%ld) game_primary=%d "
                "cursor_monitor=%s cursor_monitor_rect=(%ld,%ld)-(%ld,%ld) "
                "cursor_work=(%ld,%ld)-(%ld,%ld) cursor_primary=%d on_other_monitor=%ld",
                trigger, status, canEscape, clientScreen.left, clientScreen.top, clientScreen.right,
                clientScreen.bottom, clip.left, clip.top, clip.right, clip.bottom, desktop.left,
                desktop.top, desktop.right, desktop.bottom, cursor.x, cursor.y, outsideClient,
                GetForegroundWindow() == g_window, GetFocus() == g_window,
                IsWindowVisible(g_window), IsIconic(g_window), internalWidth, internalHeight,
                gameMonitorValid ? gameInfo.szDevice : "unknown", gameInfo.rcMonitor.left,
                gameInfo.rcMonitor.top, gameInfo.rcMonitor.right, gameInfo.rcMonitor.bottom,
                gameInfo.rcWork.left, gameInfo.rcWork.top, gameInfo.rcWork.right,
                gameInfo.rcWork.bottom, (gameInfo.dwFlags & MONITORINFOF_PRIMARY) != 0,
                cursorMonitorValid ? cursorInfo.szDevice : "unknown", cursorInfo.rcMonitor.left,
                cursorInfo.rcMonitor.top, cursorInfo.rcMonitor.right, cursorInfo.rcMonitor.bottom,
                cursorInfo.rcWork.left, cursorInfo.rcWork.top, cursorInfo.rcWork.right,
                cursorInfo.rcWork.bottom, (cursorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0,
                onOtherMonitor);
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
                QueueEvent(WM_MOUSEWHEEL, wheel, CurrentPosition());
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

void LoseFocus() {
    if (InterlockedExchange(&g_backendState, kInactive) == kInactive) return;
    logger::Log("INFO", "Focus", "focus lost; Raw Input suspended and state cleared");
    LogCursorSnapshot("before_focus_loss");
    ClearInputState();
    RECT currentClip = {};
    RECT desktop = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                    GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                    GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    if (GetClipCursor(&currentClip) && EqualRect(&currentClip, &desktop) &&
        g_hasLastValidGameClip) {
        logger::Log("INFO", "CursorClip",
                    "clip_released_before_focus_notification=1; last_valid=(%ld,%ld)-(%ld,%ld)",
                    g_lastValidGameClip.left, g_lastValidGameClip.top,
                    g_lastValidGameClip.right, g_lastValidGameClip.bottom);
    }
    const BOOL released = ClipCursor(nullptr);
    logger::Log(released ? "INFO" : "ERROR", "CursorClip",
                "source=raw_input operation=release result=%d error=%lu", released,
                released ? 0 : GetLastError());
    LogCursorSnapshot("after_focus_loss");
}

bool TryCompleteFocusRecovery(const char* trigger) {
    if (InterlockedCompareExchange(&g_backendState, kInactive, kInactive) != kRecoveryPending) {
        return false;
    }
    if (!WindowReadyForInput()) {
        logger::Log("INFO", "Focus",
                    "focus_recovery=deferred trigger=%s foreground=%d focus=%d visible=%d iconic=%d",
                    trigger, GetForegroundWindow() == g_window, GetFocus() == g_window,
                    g_window != nullptr && IsWindowVisible(g_window),
                    g_window != nullptr && IsIconic(g_window));
        return false;
    }

    RECT requested = {};
    const char* source = "recalculated_client";
    if (!GetClientScreenRect(&requested)) {
        if (!g_hasLastValidGameClip) {
            logger::Log("ERROR", "CursorClip", "focus recovery has no valid clip rectangle");
            return false;
        }
        requested = g_lastValidGameClip;
        source = "last_valid_clip";
    }

    SetLastError(0);
    const BOOL applied = ClipCursor(&requested);
    const DWORD applyError = applied ? ERROR_SUCCESS : GetLastError();
    RECT confirmed = {};
    RECT client = {};
    const bool confirmedValid = applied && GetClipCursor(&confirmed) &&
                                GetClientScreenRect(&client) &&
                                ClipIsContainedBy(confirmed, client);
    logger::Log(confirmedValid ? "INFO" : "ERROR", "CursorClip",
                "operation=restore restore_source=%s requested=(%ld,%ld)-(%ld,%ld) "
                "confirmed=(%ld,%ld)-(%ld,%ld) result=%d error=%lu",
                source, requested.left, requested.top, requested.right, requested.bottom,
                confirmed.left, confirmed.top, confirmed.right, confirmed.bottom,
                confirmedValid, applyError);
    if (!confirmedValid) return false;

    g_lastValidGameClip = confirmed;
    g_hasLastValidGameClip = true;
    ClearInputState();
    InterlockedExchange(&g_dropNextMovement, 1);
    InterlockedExchange(&g_backendState, kActive);
    logger::Log("INFO", "Focus", "focus_recovery=completed trigger=%s; Raw Input resumed", trigger);
    LogCursorSnapshot("after_focus_recovery");
    return true;
}

void RequestFocusRecovery(const char* trigger) {
    if (InterlockedCompareExchange(&g_backendState, kInactive, kInactive) == kActive) {
        RECT client = {};
        RECT clip = {};
        if (GetClientScreenRect(&client) && GetClipCursor(&clip) &&
            ClipIsContainedBy(clip, client)) {
            return;
        }
        logger::Log("WARN", "CursorClip",
                    "active backend lost valid confinement; recovery requested by %s", trigger);
    }
    InterlockedExchange(&g_backendState, kRecoveryPending);
    TryCompleteFocusRecovery(trigger);
}

void UnregisterRawInput() {
    if (InterlockedExchange(&g_registered, 0) == 0) return;
    RAWINPUTDEVICE device = {0x01, 0x02, RIDEV_REMOVE, nullptr};
    const BOOL result = RegisterRawInputDevices(&device, 1, sizeof(device));
    logger::Log(result ? "INFO" : "ERROR", "RawInput", "unregister result=%d error=%lu", result,
                result ? 0 : GetLastError());
}

LRESULT CALLBACK RawInputWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INPUT) {
        ProcessRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        return DefWindowProcW(window, message, wParam, lParam);
    }

    const LRESULT result = CallWindowProcW(g_originalWndProc, window, message, wParam, lParam);
    switch (message) {
        case WM_ACTIVATEAPP:
            wParam ? RequestFocusRecovery("WM_ACTIVATEAPP") : LoseFocus();
            break;
        case WM_ACTIVATE:
            LOWORD(wParam) == WA_INACTIVE ? LoseFocus() : RequestFocusRecovery("WM_ACTIVATE");
            break;
        case WM_SETFOCUS:
            RequestFocusRecovery("WM_SETFOCUS");
            break;
        case WM_KILLFOCUS:
            LoseFocus();
            break;
        case WM_DESTROY:
            InterlockedExchange(&g_backendState, kInactive);
            ClearInputState();
            break;
        case WM_NCDESTROY:
            InterlockedExchange(&g_backendState, kInactive);
            UnregisterRawInput();
            g_window = nullptr;
            break;
        case WM_MOVE:
            LogCursorSnapshot("WM_MOVE");
            RequestFocusRecovery("WM_MOVE");
            break;
        case WM_SIZE:
            LogCursorSnapshot(wParam == SIZE_MINIMIZED ? "WM_SIZE_MINIMIZED" : "WM_SIZE");
            if (wParam != SIZE_MINIMIZED) RequestFocusRecovery("WM_SIZE");
            break;
        case WM_DISPLAYCHANGE:
            LogCursorSnapshot("WM_DISPLAYCHANGE");
            RequestFocusRecovery("WM_DISPLAYCHANGE");
            break;
    }
    return result;
}

bool InitializeForWindow(HWND window) {
    if (window == nullptr || !IsWindow(window)) return false;
    logger::Log("INFO", "RawInput", "game window found: hwnd=0x%08lX",
                reinterpret_cast<unsigned long>(window));
    wchar_t titleWide[256] = {};
    wchar_t classWide[128] = {};
    char title[512] = {};
    char className[256] = {};
    GetWindowTextW(window, titleWide, sizeof(titleWide) / sizeof(titleWide[0]));
    GetClassNameW(window, classWide, sizeof(classWide) / sizeof(classWide[0]));
    WideCharToMultiByte(CP_UTF8, 0, titleWide, -1, title, sizeof(title), nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, classWide, -1, className, sizeof(className), nullptr, nullptr);
    logger::Log("INFO", "RawInput", "window title=%s class=%s", title, className);

    SetLastError(0);
    const LONG_PTR previous = SetWindowLongPtrW(
        window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RawInputWndProc));
    if (previous == 0 && GetLastError() != 0) {
        logger::Log("ERROR", "RawInput", "SetWindowLongPtrW failed: error=%lu", GetLastError());
        return false;
    }
    logger::Log("INFO", "RawInput", "WndProc subclass installed");

    g_window = window;
    g_originalWndProc = reinterpret_cast<WNDPROC>(previous);
    RAWINPUTDEVICE device = {
        0x01,
        0x02,
        RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE,
        window,
    };
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
        const DWORD error = GetLastError();
        SetWindowLongPtrW(window, GWLP_WNDPROC, previous);
        g_window = nullptr;
        g_originalWndProc = nullptr;
        logger::Log("ERROR", "RawInput",
                    "RegisterRawInputDevices flags=NOLEGACY|CAPTUREMOUSE failed: error=%lu; legacy fallback retained",
                    error);
        return false;
    }

    InterlockedExchange(&g_registered, 1);
    InterlockedExchange(&g_dropNextMovement, 1);
    InterlockedExchange(&g_backendState, kRecoveryPending);
    logger::Log("INFO", "RawInput",
                "registered flags=RIDEV_NOLEGACY|RIDEV_CAPTUREMOUSE; state=%s",
                "recovery_pending");
    LogCursorSnapshot("initialization");
    TryCompleteFocusRecovery("initialization");
    return true;
}

void EnsureInitialized() {
    if (g_window != nullptr || InterlockedCompareExchange(&g_initializing, 1, 0) != 0) return;
    const HWND window = *reinterpret_cast<HWND*>(kGameWindow);
    if (!InitializeForWindow(window)) InterlockedExchange(&g_initializing, 0);
}

void DrainEvents() {
    const auto dispatch = reinterpret_cast<MouseDispatcherFn>(kMouseDispatcher);
    for (;;) {
        MouseEvent event = {};
        EnterCriticalSection(&g_eventLock);
        if (g_eventRead == g_eventWrite) {
            LeaveCriticalSection(&g_eventLock);
            break;
        }
        event = g_events[g_eventRead];
        g_eventRead = (g_eventRead + 1) % kEventCapacity;
        LeaveCriticalSection(&g_eventLock);
        dispatch(event.state, event.position, event.message, 1);
    }
}

extern "C" void __cdecl RawPollMouseInput() {
    EnsureInitialized();
    if (InterlockedCompareExchange(&g_backendState, kInactive, kInactive) != kActive) {
        if (g_window == nullptr) {
            g_legacyPoll();
        } else {
            *reinterpret_cast<volatile LONG*>(kRelativeX) = 0;
            *reinterpret_cast<volatile LONG*>(kRelativeY) = 0;
        }
        return;
    }

    DrainEvents();
    *reinterpret_cast<volatile LONG*>(kMouseState) = static_cast<LONG>(CurrentState());
    const LONG x = InterlockedExchange(&g_accumX, 0);
    const LONG y = InterlockedExchange(&g_accumY, 0);
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
        const LONG overflows = InterlockedExchange(&g_queueOverflows, 0);
        g_lastStatisticsTick = now;
        logger::Log("INFO", "RawInput.Stats",
                    "interval_ms=%lu reports=%ld polls=%ld total_dx=%ld total_dy=%ld dropped=%ld "
                    "absolute_ignored=%ld size_failures=%ld read_failures=%ld queue_overflows=%ld active=%ld",
                    g_statisticsIntervalMs, reports, polls, totalX, totalY, dropped, absolute,
                    sizeFailures, readFailures, overflows,
                    InterlockedCompareExchange(&g_backendState, kInactive, kInactive) == kActive);
        LogCursorSnapshot("statistics_interval");
        RequestFocusRecovery("statistics_interval");
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
    g_statisticsIntervalMs = settings.statisticsIntervalMs;
    g_lastStatisticsTick = GetTickCount();
    InitializeCriticalSection(&g_eventLock);
    if (!WriteDetour()) {
        DeleteCriticalSection(&g_eventLock);
        logger::Log("ERROR", "RawInput", "installation failed; legacy input retained");
        return false;
    }
    return true;
}
}  // namespace raw_input
