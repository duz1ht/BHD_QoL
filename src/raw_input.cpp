#include "raw_input.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game_window.h"
#include "logger.h"
#include "raw_input_math.h"

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
constexpr uintptr_t kCameraYaw = 0x007F2DE4;
constexpr uintptr_t kCameraPitch = 0x007F2DE8;
constexpr uintptr_t kMouseSubsteps = 0x00A02248;
constexpr uintptr_t kPreviousXTerm = 0x009F3750;
constexpr uintptr_t kPreviousYTerm = 0x009FB280;
constexpr wchar_t kInputWindowClass[] = L"BHD_QoL_RawInputSink";

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
volatile LONG g_dropNextMovement = 0;
volatile LONG g_droppedPackets = 0;
volatile LONG g_reportCount = 0;
volatile LONG g_pollCount = 0;
volatile LONG g_pollSerial = 0;
volatile LONG g_rawPacketSerial = 0;
volatile LONG g_directPacketSerial = 0;
volatile LONG g_bufferedPacketSerial = 0;
volatile LONG g_bufferDrainSerial = 0;
volatile LONG g_bufferReadFailures = 0;
volatile LONG g_lastConsumedX = 0;
volatile LONG g_lastConsumedY = 0;
volatile LONG g_cameraYawAtPoll = 0;
volatile LONG g_cameraPitchAtPoll = 0;
alignas(8) volatile LONG64 g_lastPollCounter = 0;
alignas(8) volatile LONG64 g_pollIntervalCounter = 0;
LONGLONG g_performanceFrequency = 0;
volatile LONG g_intervalX = 0;
volatile LONG g_intervalY = 0;
volatile LONG g_absoluteReports = 0;
volatile LONG g_sizeFailures = 0;
volatile LONG g_readFailures = 0;
bool g_enabled = false;
DWORD g_statisticsIntervalMs = 5000;
DWORD g_lastStatisticsTick = 0;
HWND g_window = nullptr;
HWND g_inputWindow = nullptr;
HANDLE g_inputReadyEvent = nullptr;
volatile LONG g_inputThreadStarted = 0;
PollMouseInputFn g_legacyPoll = nullptr;
HANDLE g_loggedDevices[16] = {};
size_t g_loggedDeviceCount = 0;
constexpr size_t kRawInputBufferSize = 64 * 1024;
alignas(8) unsigned char g_rawInputBuffer[kRawInputBufferSize] = {};

void ProcessRawInput(HRAWINPUT handle);
void DrainRawInputQueue();

LRESULT CALLBACK InputWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INPUT) {
        ProcessRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        DrainRawInputQueue();
        return DefWindowProcW(window, message, wParam, lParam);
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

DWORD WINAPI InputThreadProc(void*) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = InputWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kInputWindowClass;
    if (RegisterClassW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (g_inputReadyEvent != nullptr) SetEvent(g_inputReadyEvent);
        return 1;
    }

    g_inputWindow = CreateWindowExW(0, kInputWindowClass, L"", 0, 0, 0, 0, 0,
                                    HWND_MESSAGE, nullptr, instance, nullptr);
    if (g_inputReadyEvent != nullptr) SetEvent(g_inputReadyEvent);
    if (g_inputWindow == nullptr) return 2;

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    g_inputWindow = nullptr;
    return 0;
}

bool EnsureInputThread() {
    if (g_inputWindow != nullptr) return true;
    if (InterlockedCompareExchange(&g_inputThreadStarted, 1, 0) == 0) {
        g_inputReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (g_inputReadyEvent == nullptr) return false;
        HANDLE thread = CreateThread(nullptr, 0, InputThreadProc, nullptr, 0, nullptr);
        if (thread == nullptr) {
            CloseHandle(g_inputReadyEvent);
            g_inputReadyEvent = nullptr;
            return false;
        }
        CloseHandle(thread);
    }
    if (g_inputReadyEvent == nullptr ||
        WaitForSingleObject(g_inputReadyEvent, 3000) != WAIT_OBJECT_0) {
        return false;
    }
    return g_inputWindow != nullptr;
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
    DWORD gameWindowThread = 0;
    if (g_window != nullptr) {
        gameWindowThread = GetWindowThreadProcessId(g_window, nullptr);
    }
    if (gameWindowThread != 0 && gameWindowThread != GetCurrentThreadId()) {
        const BOOL posted = PostMessageW(g_window, message, state, position);
        logger::Log(posted ? "INFO" : "ERROR", "RawInput.Dispatch",
                    "posted message=0x%04X state=0x%04lX x=%d y=%d error=%lu",
                    message, static_cast<unsigned long>(state),
                    static_cast<int>(static_cast<short>(LOWORD(position))),
                    static_cast<int>(static_cast<short>(HIWORD(position))),
                    posted ? ERROR_SUCCESS : GetLastError());
        return;
    }
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

void ProcessRawInputRecord(const RAWINPUT* input, bool buffered) {
    if (input == nullptr || input->header.dwType != RIM_TYPEMOUSE ||
        InterlockedCompareExchange(&g_backendState, kInactive, kInactive) != kActive) {
        return;
    }

    LogDevice(input->header.hDevice);
    InterlockedIncrement(&g_reportCount);
    const RAWMOUSE& mouse = input->data.mouse;
    if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
        InterlockedIncrement(&g_rawPacketSerial);
        InterlockedIncrement(buffered ? &g_bufferedPacketSerial : &g_directPacketSerial);
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
        DispatchEvent(WM_MOUSEWHEEL, wheel, CurrentPosition());
        logger::Log("INFO", "RawInput.Wheel", "delta=%d state=0x%04lX",
                    static_cast<short>(mouse.usButtonData),
                    static_cast<unsigned long>(CurrentState()));
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
        ProcessRawInputRecord(input, false);
    } else {
        InterlockedIncrement(&g_droppedPackets);
        InterlockedIncrement(&g_readFailures);
        logger::Log("ERROR", "RawInput", "GetRawInputData read failed: expected=%u actual=%u error=%lu",
                    size, read, GetLastError());
    }

    if (buffer != stackBuffer) HeapFree(GetProcessHeap(), 0, buffer);
}

void DrainRawInputQueue() {
    for (;;) {
        UINT bufferSize = static_cast<UINT>(sizeof(g_rawInputBuffer));
        const UINT count = GetRawInputBuffer(
            reinterpret_cast<PRAWINPUT>(g_rawInputBuffer), &bufferSize,
            sizeof(RAWINPUTHEADER));
        if (count == 0) return;
        if (count == static_cast<UINT>(-1)) {
            InterlockedIncrement(&g_bufferReadFailures);
            InterlockedIncrement(&g_droppedPackets);
            logger::Log("ERROR", "RawInput",
                        "GetRawInputBuffer failed: buffer_size=%u error=%lu",
                        bufferSize, GetLastError());
            return;
        }

        InterlockedIncrement(&g_bufferDrainSerial);
        PRAWINPUT input = reinterpret_cast<PRAWINPUT>(g_rawInputBuffer);
        for (UINT index = 0; index < count; ++index) {
            ProcessRawInputRecord(input, true);
            input = NEXTRAWINPUTBLOCK(input);
        }
    }
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
    if (!EnsureInputThread()) {
        logger::Log("ERROR", "RawInput", "dedicated input window was not available");
        return false;
    }
    g_window = window;
    RAWINPUTDEVICE device = {
        0x01, 0x02,
        RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE | RIDEV_INPUTSINK,
        g_inputWindow,
    };
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
        const DWORD captureError = GetLastError();
        device.dwFlags = RIDEV_NOLEGACY | RIDEV_INPUTSINK;
        if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
            logger::Log("ERROR", "RawInput",
                        "dedicated RegisterRawInputDevices failed: capture_error=%lu "
                        "fallback_error=%lu",
                        captureError, GetLastError());
            return false;
        }
        logger::Log("WARN", "RawInput",
                    "CAPTUREMOUSE registration failed; INPUTSINK fallback active error=%lu",
                    captureError);
    }
    InterlockedExchange(&g_registered, 1);
    InterlockedExchange(&g_backendState, kRecoveryPending);
    logger::Log("INFO", "RawInput",
                "mouse registered on dedicated input thread hwnd=0x%08lX; "
                "state=recovery_pending",
                reinterpret_cast<unsigned long>(g_inputWindow));
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
    InterlockedIncrement(&g_pollSerial);
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
    LARGE_INTEGER pollNow = {};
    if (QueryPerformanceCounter(&pollNow)) {
        const LONG64 previousPoll = InterlockedExchange64(
            &g_lastPollCounter, pollNow.QuadPart);
        const LONG64 measuredInterval = pollNow.QuadPart - previousPoll;
        if (previousPoll > 0 && measuredInterval > 0 &&
            (g_performanceFrequency <= 0 ||
             (measuredInterval >= g_performanceFrequency / 250 &&
              measuredInterval <= g_performanceFrequency / 25))) {
            InterlockedExchange64(&g_pollIntervalCounter, measuredInterval);
        }
    }
    InterlockedExchange(&g_cameraYawAtPoll,
                        *reinterpret_cast<volatile LONG*>(kCameraYaw));
    InterlockedExchange(&g_cameraPitchAtPoll,
                        *reinterpret_cast<volatile LONG*>(kCameraPitch));
    const LONG x = InterlockedExchange(&g_accumX, 0);
    const LONG y = InterlockedExchange(&g_accumY, 0);
    InterlockedExchange(&g_lastConsumedX, x);
    InterlockedExchange(&g_lastConsumedY, y);
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
        g_lastStatisticsTick = now;
        logger::Log("INFO", "RawInput.Stats",
                    "interval_ms=%lu reports=%ld polls=%ld total_dx=%ld total_dy=%ld dropped=%ld "
                    "absolute_ignored=%ld size_failures=%ld read_failures=%ld active=%ld",
                    g_statisticsIntervalMs, reports, polls, totalX, totalY, dropped, absolute,
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
    LARGE_INTEGER frequency = {};
    if (QueryPerformanceFrequency(&frequency)) {
        g_performanceFrequency = frequency.QuadPart;
        InterlockedExchange64(&g_pollIntervalCounter,
                              (frequency.QuadPart * 16) / 1000);
    }
    if (!EnsureInputThread()) {
        g_enabled = false;
        logger::Log("ERROR", "RawInput",
                    "dedicated input thread initialization failed: error=%lu",
                    GetLastError());
        return false;
    }
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
    if (g_inputWindow != nullptr) PostMessageW(g_inputWindow, WM_CLOSE, 0, 0);
}
bool IsEnabled() { return g_enabled; }
bool GetPendingMovement(LONG* x, LONG* y) {
    if (x == nullptr || y == nullptr || !g_enabled ||
        InterlockedCompareExchange(&g_backendState, kInactive, kInactive) != kActive) {
        return false;
    }
    *x = InterlockedCompareExchange(&g_accumX, 0, 0);
    *y = InterlockedCompareExchange(&g_accumY, 0, 0);
    return true;
}
bool GetProjectedNativeMovement(LONG* x, LONG* y, uint32_t* phaseQ16) {
    if (x == nullptr || y == nullptr || !g_enabled ||
        InterlockedCompareExchange(&g_backendState, kInactive, kInactive) != kActive) {
        return false;
    }
    int divisor = *reinterpret_cast<const volatile int*>(kMouseSubsteps);
    if (divisor <= 0) divisor = 1;
    const LONG pendingX = InterlockedCompareExchange(&g_accumX, 0, 0);
    const LONG pendingY = InterlockedCompareExchange(&g_accumY, 0, 0);
    uint32_t phase = 0x10000U;
    const LONG64 lastPoll = InterlockedCompareExchange64(&g_lastPollCounter, 0, 0);
    LONG64 interval = InterlockedCompareExchange64(&g_pollIntervalCounter, 0, 0);
    LARGE_INTEGER now = {};
    if (lastPoll > 0 && interval > 0 && QueryPerformanceCounter(&now)) {
        LONG64 elapsed = now.QuadPart - lastPoll;
        if (elapsed < 0) elapsed = 0;
        if (elapsed < interval) {
            phase = static_cast<uint32_t>((elapsed * 0x10000) / interval);
        }
    }
    *x = ProjectPresentationAxis(
        pendingX, *reinterpret_cast<const volatile LONG*>(kPreviousXTerm), divisor, phase);
    *y = ProjectPresentationAxis(
        pendingY, *reinterpret_cast<const volatile LONG*>(kPreviousYTerm), divisor, phase);
    if (phaseQ16 != nullptr) *phaseQ16 = phase;
    return true;
}
void GetTraceCounters(unsigned long* pollSerial, unsigned long* rawPacketSerial) {
    if (pollSerial != nullptr) {
        *pollSerial = static_cast<unsigned long>(
            InterlockedCompareExchange(&g_pollSerial, 0, 0));
    }
    if (rawPacketSerial != nullptr) {
        *rawPacketSerial = static_cast<unsigned long>(
            InterlockedCompareExchange(&g_rawPacketSerial, 0, 0));
    }
}
void GetTraceSnapshot(TraceSnapshot* snapshot) {
    if (snapshot == nullptr) return;
    snapshot->pollSerial = static_cast<unsigned long>(
        InterlockedCompareExchange(&g_pollSerial, 0, 0));
    snapshot->rawPacketSerial = static_cast<unsigned long>(
        InterlockedCompareExchange(&g_rawPacketSerial, 0, 0));
    snapshot->directPacketSerial = static_cast<unsigned long>(
        InterlockedCompareExchange(&g_directPacketSerial, 0, 0));
    snapshot->bufferedPacketSerial = static_cast<unsigned long>(
        InterlockedCompareExchange(&g_bufferedPacketSerial, 0, 0));
    snapshot->bufferDrainSerial = static_cast<unsigned long>(
        InterlockedCompareExchange(&g_bufferDrainSerial, 0, 0));
    snapshot->bufferReadFailures = static_cast<unsigned long>(
        InterlockedCompareExchange(&g_bufferReadFailures, 0, 0));
    snapshot->consumedX = InterlockedCompareExchange(&g_lastConsumedX, 0, 0);
    snapshot->consumedY = InterlockedCompareExchange(&g_lastConsumedY, 0, 0);
    snapshot->cameraYawAtPoll = InterlockedCompareExchange(&g_cameraYawAtPoll, 0, 0);
    snapshot->cameraPitchAtPoll = InterlockedCompareExchange(&g_cameraPitchAtPoll, 0, 0);
}
}  // namespace raw_input
