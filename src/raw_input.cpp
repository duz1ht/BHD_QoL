#include "raw_input.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game_window.h"
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
volatile LONG g_pollSerial = 0;
volatile LONG g_dropNextMovement = 0;
alignas(8) volatile LONG64 g_lastPollCounter = 0;
alignas(8) volatile LONG64 g_pollIntervalCounter = 0;
LONGLONG g_performanceFrequency = 0;
bool g_enabled = false;
HWND g_window = nullptr;
HWND g_inputWindow = nullptr;
HANDLE g_inputReadyEvent = nullptr;
volatile LONG g_inputThreadStarted = 0;
PollMouseInputFn g_legacyPoll = nullptr;
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

bool WindowHasInputFocus() {
    const HWND focus = GetFocus();
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
        PostMessageW(g_window, message, state, position);
        return;
    }
    const auto dispatch = reinterpret_cast<MouseDispatcherFn>(kMouseDispatcher);
    dispatch(state, position, message, 1);
}

void ClearInputState() {
    InterlockedExchange(&g_accumX, 0);
    InterlockedExchange(&g_accumY, 0);
    InterlockedExchange(&g_buttonState, 0);
}

void DispatchButton(LONG stateBit, bool pressed, UINT message) {
    LONG oldState = InterlockedCompareExchange(&g_buttonState, 0, 0);
    LONG newState = 0;
    do {
        newState = pressed ? oldState | stateBit : oldState & ~stateBit;
        const LONG observed = InterlockedCompareExchange(&g_buttonState, newState, oldState);
        if (observed == oldState) break;
        oldState = observed;
    } while (true);
    const WPARAM state = static_cast<WPARAM>(newState);
    DispatchEvent(message, state | (CurrentState() & (MK_SHIFT | MK_CONTROL)),
                  CurrentPosition());
}

void SetButton(USHORT flags, USHORT downFlag, USHORT upFlag, LONG stateBit,
               UINT downMessage, UINT upMessage) {
    if ((flags & downFlag) != 0) DispatchButton(stateBit, true, downMessage);
    if ((flags & upFlag) != 0) DispatchButton(stateBit, false, upMessage);
}


void ProcessRawInputRecord(const RAWINPUT* input) {
    if (input == nullptr || input->header.dwType != RIM_TYPEMOUSE ||
        InterlockedCompareExchange(&g_backendState, kInactive, kInactive) != kActive) {
        return;
    }

    const RAWMOUSE& mouse = input->data.mouse;
    if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
        if (InterlockedExchange(&g_dropNextMovement, 0) == 0) {
            InterlockedExchangeAdd(&g_accumX, mouse.lLastX);
            InterlockedExchangeAdd(&g_accumY, mouse.lLastY);
            UpdateVirtualCursor(mouse.lLastX, mouse.lLastY);
        }
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
    }
}

void ProcessRawInput(HRAWINPUT handle) {
    UINT size = 0;
    if (GetRawInputData(handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 ||
        size == 0) {
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
        ProcessRawInputRecord(input);
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
            return;
        }

        PRAWINPUT input = reinterpret_cast<PRAWINPUT>(g_rawInputBuffer);
        for (UINT index = 0; index < count; ++index) {
            ProcessRawInputRecord(input);
            input = NEXTRAWINPUTBLOCK(input);
        }
    }
}

void SuspendInput() {
    if (InterlockedExchange(&g_backendState, kInactive) == kInactive) return;
    ClearInputState();
}

bool RegisterForWindow(HWND window);

bool ResumeInput() {
    if (g_window == nullptr || !IsWindow(g_window) ||
        !IsWindowVisible(g_window) || IsIconic(g_window) ||
        GetForegroundWindow() != g_window || !WindowHasInputFocus()) {
        InterlockedExchange(&g_backendState, kRecoveryPending);
        return false;
    }
    if (InterlockedCompareExchange(&g_registered, 0, 0) == 0 &&
        !RegisterForWindow(g_window)) {
        InterlockedExchange(&g_backendState, kRecoveryPending);
        return false;
    }
    if (InterlockedCompareExchange(&g_backendState, kInactive, kInactive) == kActive) {
        return true;
    }
    ClearInputState();
    InterlockedExchange(&g_dropNextMovement, 1);
    InterlockedExchange(&g_backendState, kActive);
    InterlockedExchange(&g_activatedOnce, 1);
    return true;
}

bool RegisterForWindow(HWND window) {
    if (!g_enabled) return true;
    if (g_window == window && InterlockedCompareExchange(&g_registered, 0, 0)) return true;
    if (window == nullptr || !IsWindow(window)) return false;
    if (!EnsureInputThread()) {
        return false;
    }
    g_window = window;
    RAWINPUTDEVICE device = {
        0x01, 0x02,
        RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE | RIDEV_INPUTSINK,
        g_inputWindow,
    };
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
        device.dwFlags = RIDEV_NOLEGACY | RIDEV_INPUTSINK;
        if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
            return false;
        }
    }
    InterlockedExchange(&g_registered, 1);
    InterlockedExchange(&g_backendState, kRecoveryPending);
    return true;
}

void UnregisterRawInput() {
    if (InterlockedExchange(&g_registered, 0) == 0) return;
    RAWINPUTDEVICE device = {0x01, 0x02, RIDEV_REMOVE, nullptr};
    RegisterRawInputDevices(&device, 1, sizeof(device));
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
    const LONG x = InterlockedExchange(&g_accumX, 0);
    const LONG y = InterlockedExchange(&g_accumY, 0);
    *reinterpret_cast<volatile LONG*>(kRelativeX) = x;
    *reinterpret_cast<volatile LONG*>(kRelativeY) = y;
}

bool WriteDetour() {
    auto* target = reinterpret_cast<unsigned char*>(kPollMouseInput);
    if (std::memcmp(target, kExpectedPollBytes, kDetourSize) != 0) {
        return false;
    }

    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, kDetourSize + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
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
        return false;
    }
    std::memcpy(target, detour, sizeof(detour));
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(detour));
    DWORD ignored = 0;
    VirtualProtect(target, sizeof(detour), oldProtect, &ignored);
    return true;
}
}  // namespace

bool Install(bool enabled) {
    if (!enabled) {
        return true;
    }
    g_enabled = true;
    LARGE_INTEGER frequency = {};
    if (QueryPerformanceFrequency(&frequency)) {
        g_performanceFrequency = frequency.QuadPart;
        InterlockedExchange64(&g_pollIntervalCounter,
                              (frequency.QuadPart * 16) / 1000);
    }
    if (!EnsureInputThread()) {
        g_enabled = false;
        return false;
    }
    if (!WriteDetour()) {
        g_enabled = false;
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
bool HandleFocusGained() {
    return !g_enabled || ResumeInput();
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
bool GetProjectedNativeMovement(LONG* x, LONG* y) {
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
    return true;
}
unsigned long GetSimulationSerial() {
    return static_cast<unsigned long>(InterlockedCompareExchange(&g_pollSerial, 0, 0));
}

}  // namespace raw_input
