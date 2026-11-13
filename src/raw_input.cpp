#include "raw_input.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game_window.h"
#include "raw_input_math.h"
#include "raw_input_packets.h"

namespace raw_input {
namespace {
constexpr uintptr_t kPollMouseInput = 0x005678F0;
constexpr uintptr_t kGameWindow = 0x00F654FC;
constexpr uintptr_t kRelativeX = 0x00F655EC;
constexpr uintptr_t kRelativeY = 0x00F655F0;
constexpr uintptr_t kMouseSubsteps = 0x00A02248;
constexpr uintptr_t kPreviousXTerm = 0x009F3750;
constexpr uintptr_t kPreviousYTerm = 0x009FB280;
constexpr wchar_t kInputWindowClass[] = L"BHD_QoL_RawInputSink";

// Copy whole instructions: sub esp,24h; push esi; mov esi,[006112BCh].
// A six-byte trampoline would split the final instruction's address operand.
constexpr unsigned char kExpectedPollBytes[] = {
    0x83, 0xEC, 0x24, 0x56, 0x8B, 0x35, 0xBC, 0x12, 0x61, 0x00};
constexpr size_t kDetourSize = sizeof(kExpectedPollBytes);
using PollMouseInputFn = void(__cdecl*)();

volatile LONG g_accumX = 0;
volatile LONG g_accumY = 0;
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
constexpr unsigned int kMaximumDrainBatches = 4;
size_t g_bufferedHeaderSize = sizeof(RAWINPUTHEADER);
size_t g_bufferedAlignment = sizeof(void*);
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

void ClearInputState() {
    InterlockedExchange(&g_accumX, 0);
    InterlockedExchange(&g_accumY, 0);
}

void ProcessRawMouse(const RAWMOUSE& mouse) {
    if (InterlockedCompareExchange(&g_backendState, kInactive, kInactive) != kActive ||
        GetForegroundWindow() != g_window) {
        return;
    }
    // Buttons, wheel and cursor coordinates remain on the game's native message
    // path. The input thread only publishes relative movement, never game memory
    // writes or synthetic WM_*BUTTON/WM_MOUSEWHEEL messages.
    if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0 &&
        (mouse.lLastX != 0 || mouse.lLastY != 0) &&
        InterlockedExchange(&g_dropNextMovement, 0) == 0) {
        InterlockedExchangeAdd(&g_accumX, mouse.lLastX);
        InterlockedExchangeAdd(&g_accumY, mouse.lLastY);
    }
}

void ProcessRawInput(HRAWINPUT handle) {
    UINT size = 0;
    if (GetRawInputData(handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 ||
        size == 0) {
        return;
    }

    alignas(8) unsigned char stackBuffer[sizeof(RAWINPUT) + 64] = {};
    unsigned char* buffer = stackBuffer;
    if (size > sizeof(stackBuffer)) {
        buffer = static_cast<unsigned char*>(HeapAlloc(GetProcessHeap(), 0, size));
        if (buffer == nullptr) return;
    }

    const UINT read = GetRawInputData(handle, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER));
    if (read != static_cast<UINT>(-1) && read == size) {
        VisitRawInputPackets(buffer, read, 1, sizeof(RAWINPUTHEADER),
                             sizeof(void*), ProcessRawMouse);
    }

    if (buffer != stackBuffer) HeapFree(GetProcessHeap(), 0, buffer);
}

void DrainRawInputQueue() {
    // Leave remaining WM_INPUT messages in the queue so control messages and
    // shutdown get a turn even when a high polling-rate mouse keeps producing.
    for (unsigned int batch = 0; batch < kMaximumDrainBatches; ++batch) {
        UINT bufferSize = static_cast<UINT>(sizeof(g_rawInputBuffer));
        const UINT count = GetRawInputBuffer(
            reinterpret_cast<PRAWINPUT>(g_rawInputBuffer), &bufferSize,
            sizeof(RAWINPUTHEADER));
        if (count == 0) return;
        if (count == static_cast<UINT>(-1)) {
            return;
        }

        if (!VisitRawInputPackets(g_rawInputBuffer, sizeof(g_rawInputBuffer), count,
                                  g_bufferedHeaderSize, g_bufferedAlignment,
                                  ProcessRawMouse)) return;
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
    // Foreground-only registration; keep native button/wheel messages and allow
    // clicks to activate other applications normally.
    RAWINPUTDEVICE device = {0x01, 0x02, 0, g_inputWindow};
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) return false;
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
    FlushInstructionCache(GetCurrentProcess(), trampoline, kDetourSize + 5);
    g_legacyPoll = reinterpret_cast<PollMouseInputFn>(trampoline);

    unsigned char detour[kDetourSize];
    std::memset(detour, 0x90, sizeof(detour));
    detour[0] = 0xE9;
    *reinterpret_cast<int32_t*>(detour + 1) = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(&RawPollMouseInput) - (kPollMouseInput + 5));
    DWORD oldProtect = 0;
    if (!VirtualProtect(target, sizeof(detour), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        g_legacyPoll = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
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
    BOOL wow64 = FALSE;
    if (!IsWow64Process(GetCurrentProcess(), &wow64)) return false;
    g_bufferedHeaderSize = BufferedHeaderSize(wow64 != FALSE);
    g_bufferedAlignment = wow64 ? 8 : sizeof(void*);
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
