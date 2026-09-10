#include "raw_input.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

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
volatile LONG g_active = 0;
volatile LONG g_initializing = 0;
volatile LONG g_dropNextMovement = 0;
volatile LONG g_droppedPackets = 0;
volatile LONG g_reportCount = 0;
volatile LONG g_pollCount = 0;
bool g_debug = false;
HWND g_window = nullptr;
WNDPROC g_originalWndProc = nullptr;
PollMouseInputFn g_legacyPoll = nullptr;
CRITICAL_SECTION g_eventLock;
MouseEvent g_events[kEventCapacity] = {};
size_t g_eventRead = 0;
size_t g_eventWrite = 0;
RECT g_savedClip = {};
bool g_restoreClip = false;

void Debug(const char* message) {
    if (g_debug) {
        OutputDebugStringA(message);
    }
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

void QueueEvent(UINT message, WPARAM state, LPARAM position) {
    EnterCriticalSection(&g_eventLock);
    const size_t next = (g_eventWrite + 1) % kEventCapacity;
    if (next == g_eventRead) {
        g_eventRead = (g_eventRead + 1) % kEventCapacity;
        InterlockedIncrement(&g_droppedPackets);
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
    }
}

void ProcessRawInput(HRAWINPUT handle) {
    UINT size = 0;
    if (GetRawInputData(handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 ||
        size == 0) {
        InterlockedIncrement(&g_droppedPackets);
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
        if (input->header.dwType == RIM_TYPEMOUSE && InterlockedCompareExchange(&g_active, 0, 0)) {
            InterlockedIncrement(&g_reportCount);
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
                QueueEvent(WM_MOUSEWHEEL, wheel, CurrentPosition());
            }
        }
    } else {
        InterlockedIncrement(&g_droppedPackets);
    }

    if (buffer != stackBuffer) HeapFree(GetProcessHeap(), 0, buffer);
}

void LoseFocus() {
    if (InterlockedExchange(&g_active, 0) == 0) return;
    ClearInputState();
    g_restoreClip = false;
    RECT clip = {};
    RECT desktop = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                    GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                    GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    if (GetClipCursor(&clip) && !EqualRect(&clip, &desktop)) {
        g_savedClip = clip;
        g_restoreClip = true;
        ClipCursor(nullptr);
    }
}

void GainFocus() {
    if (InterlockedCompareExchange(&g_active, 0, 0) != 0) return;
    ClearInputState();
    InterlockedExchange(&g_dropNextMovement, 1);
    if (g_restoreClip && IsWindowVisible(g_window) && !IsIconic(g_window)) {
        ClipCursor(&g_savedClip);
    }
    InterlockedExchange(&g_active, 1);
}

void UnregisterRawInput() {
    RAWINPUTDEVICE device = {0x01, 0x02, RIDEV_REMOVE, nullptr};
    RegisterRawInputDevices(&device, 1, sizeof(device));
}

LRESULT CALLBACK RawInputWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INPUT:
            ProcessRawInput(reinterpret_cast<HRAWINPUT>(lParam));
            return DefWindowProcW(window, message, wParam, lParam);
        case WM_ACTIVATEAPP:
            wParam ? GainFocus() : LoseFocus();
            break;
        case WM_ACTIVATE:
            LOWORD(wParam) == WA_INACTIVE ? LoseFocus() : GainFocus();
            break;
        case WM_SETFOCUS:
            GainFocus();
            break;
        case WM_KILLFOCUS:
            LoseFocus();
            break;
        case WM_DESTROY:
        case WM_NCDESTROY:
            InterlockedExchange(&g_active, 0);
            UnregisterRawInput();
            break;
    }
    return CallWindowProcW(g_originalWndProc, window, message, wParam, lParam);
}

bool InitializeForWindow(HWND window) {
    if (window == nullptr || !IsWindow(window)) return false;

    SetLastError(0);
    const LONG_PTR previous = SetWindowLongPtrW(
        window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RawInputWndProc));
    if (previous == 0 && GetLastError() != 0) return false;

    g_window = window;
    g_originalWndProc = reinterpret_cast<WNDPROC>(previous);
    RAWINPUTDEVICE device = {
        0x01,
        0x02,
        RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE,
        window,
    };
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
        SetWindowLongPtrW(window, GWLP_WNDPROC, previous);
        g_window = nullptr;
        g_originalWndProc = nullptr;
        return false;
    }

    InterlockedExchange(&g_dropNextMovement, 1);
    InterlockedExchange(&g_active, 1);
    Debug("BHD_QoL Raw Input: registered mouse and installed WndProc\n");
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
    if (InterlockedCompareExchange(&g_active, 0, 0) == 0) {
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
    *reinterpret_cast<volatile LONG*>(kRelativeX) = x;
    *reinterpret_cast<volatile LONG*>(kRelativeY) = y;
    const LONG polls = InterlockedIncrement(&g_pollCount);
    if (g_debug && (polls % 600) == 0) {
        char message[192] = {};
        wsprintfA(message,
                  "BHD_QoL Raw Input: reports=%ld polls=%ld delta=%ld,%ld dropped=%ld focus=%ld\n",
                  InterlockedCompareExchange(&g_reportCount, 0, 0), polls, x, y,
                  InterlockedCompareExchange(&g_droppedPackets, 0, 0),
                  InterlockedCompareExchange(&g_active, 0, 0));
        OutputDebugStringA(message);
    }
}

bool WriteDetour() {
    auto* target = reinterpret_cast<unsigned char*>(kPollMouseInput);
    if (std::memcmp(target, kExpectedPollBytes, kDetourSize) != 0) return false;

    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, kDetourSize + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) return false;
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
    if (!VirtualProtect(target, sizeof(detour), PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    std::memcpy(target, detour, sizeof(detour));
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(detour));
    DWORD ignored = 0;
    VirtualProtect(target, sizeof(detour), oldProtect, &ignored);
    return true;
}
}  // namespace

bool Install(const Settings& settings) {
    if (!settings.enabled) return true;
    g_debug = settings.debug;
    InitializeCriticalSection(&g_eventLock);
    if (!WriteDetour()) {
        DeleteCriticalSection(&g_eventLock);
        Debug("BHD_QoL Raw Input: unsupported PollMouseInput bytes; legacy input retained\n");
        return false;
    }
    Debug("BHD_QoL Raw Input: PollMouseInput hook installed\n");
    return true;
}
}  // namespace raw_input
