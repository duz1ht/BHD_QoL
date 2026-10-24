#include "game_window.h"

#include <cstdint>

#include "cursor_clip.h"
#include "cursor_visibility.h"
#include "borderless_fullscreen.h"
#include "borderless_gamma.h"
#include "raw_input.h"
#include "mouse_scaling_fix.h"

namespace game_window {
namespace {
Settings g_settings = {};
HWND g_window = nullptr;
WNDPROC g_originalWndProc = nullptr;
volatile LONG g_installing = 0;
constexpr uintptr_t kGameWindow = 0x00F654FC;
constexpr UINT kInitialFocusRecoveryMessage = WM_APP + 0x42;
constexpr UINT_PTR kBorderlessVerificationTimer = 0xB4D;
constexpr UINT_PTR kBorderlessGammaTimer = 0xB4E;
constexpr UINT_PTR kForegroundVerificationTimer = 0xB4F;
unsigned int g_borderlessVerificationTicks = 0;
enum class ActivationState { Unknown, Inactive, ActivationPending, Active };
ActivationState g_activationState = ActivationState::Unknown;


bool IsGameForeground() {
    const HWND foreground = GetForegroundWindow();
    const HWND root = foreground == nullptr ? nullptr : GetAncestor(foreground, GA_ROOT);
    DWORD process = 0;
    if (foreground != nullptr) GetWindowThreadProcessId(foreground, &process);
    return foreground != nullptr && !IsIconic(g_window) &&
           (root == g_window || process == GetCurrentProcessId());
}

bool WindowHasInputFocus() {
    const HWND focus = GetFocus();
    return GetForegroundWindow() == g_window &&
           (focus == g_window || (focus != nullptr && GetAncestor(focus, GA_ROOT) == g_window));
}

bool TryResume() {
    if (!WindowHasInputFocus()) {
        return false;
    }
    if (!cursor_clip::HandleFocusGained()) return false;
    if (g_settings.rawMouseInput && !raw_input::HandleFocusGained()) return false;
    cursor_visibility::HandleGameActivated();
    if (g_settings.borderlessGamma) borderless_gamma::HandleFocusGained(g_window);
    return true;
}

void HandleFocusLost() {
    if (g_settings.borderlessGamma) borderless_gamma::HandleFocusLost();
    mouse_scaling_fix::Reset();
    if (g_settings.rawMouseInput) raw_input::HandleFocusLost();
    cursor_clip::HandleFocusLost();
    cursor_visibility::HandleGameDeactivated();
}

void UpdateForegroundState() {
    if (!IsGameForeground()) {
        if (g_activationState == ActivationState::Inactive) return;
        g_activationState = ActivationState::Inactive;
        HandleFocusLost();
        return;
    }

    if (g_activationState == ActivationState::Active) return;
    if (g_activationState != ActivationState::ActivationPending) {
        g_activationState = ActivationState::ActivationPending;
    }
    if (TryResume()) {
        g_activationState = ActivationState::Active;
    }
}

void HandleInitialFocusRecovery(HWND window) {
    if (!g_settings.rawMouseInput || !IsWindowVisible(window) || IsIconic(window) ||
        GetForegroundWindow() != window) {
        return;
    }
    if (GetFocus() == nullptr) SetFocus(window);
    UpdateForegroundState();
}

LRESULT CALLBACK SharedWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INPUT && g_settings.rawMouseInput) {
        raw_input::HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        return DefWindowProcW(window, message, wParam, lParam);
    }
    if (message == kInitialFocusRecoveryMessage) {
        HandleInitialFocusRecovery(window);
        return 0;
    }
    const LRESULT result = CallWindowProcW(g_originalWndProc, window, message, wParam, lParam);
    switch (message) {
        case WM_ACTIVATEAPP:
        case WM_ACTIVATE:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            UpdateForegroundState();
            break;
        case WM_MOVE:
        case WM_DISPLAYCHANGE:
            borderless_fullscreen::Apply(window);
            cursor_clip::HandleWindowChanged();
            UpdateForegroundState();
            if (message == WM_DISPLAYCHANGE && g_settings.borderlessGamma &&
                g_activationState == ActivationState::Active) {
                borderless_gamma::HandleDisplayChanged(window);
            }
            break;
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) borderless_fullscreen::Apply(window);
            cursor_clip::HandleWindowChanged();
            UpdateForegroundState();
            break;
        case WM_TIMER:
            if (wParam == kForegroundVerificationTimer) {
                UpdateForegroundState();
            }
            if (wParam == kBorderlessGammaTimer &&
                g_activationState == ActivationState::Active) borderless_gamma::Poll(window);
            if (wParam == kBorderlessVerificationTimer) {
                borderless_fullscreen::Apply(window);
                if (++g_borderlessVerificationTicks >= 20) {
                    KillTimer(window, kBorderlessVerificationTimer);
                }
            }
            break;
        case WM_DESTROY:
            if (g_activationState != ActivationState::Inactive) {
                g_activationState = ActivationState::Inactive;
                HandleFocusLost();
            }
            break;
        case WM_NCDESTROY:
            KillTimer(window, kForegroundVerificationTimer);
            if (g_settings.borderlessGamma) {
                KillTimer(window, kBorderlessGammaTimer);
                borderless_gamma::Shutdown();
            }
            raw_input::HandleDestroy();
            cursor_clip::Shutdown();
            cursor_visibility::Shutdown();
            g_window = nullptr;
            g_originalWndProc = nullptr;
            g_activationState = ActivationState::Unknown;
            InterlockedExchange(&g_installing, 0);
            break;
    }
    return result;
}

DWORD WINAPI WindowDiscoveryThread(void*) {
    for (unsigned int attempt = 0; attempt < 600; ++attempt) {
        const HWND window = *reinterpret_cast<HWND*>(kGameWindow);
        if (EnsureInstalled(window)) return 0;
        Sleep(50);
    }
    return 1;
}
}  // namespace

void Configure(const Settings& settings) {
    g_settings = settings;
    if ((!settings.rawMouseInput && (settings.restoreCursorClip || settings.mouseScalingFix)) ||
        settings.borderlessFullscreen) {
        HANDLE thread = CreateThread(nullptr, 0, WindowDiscoveryThread, nullptr, 0, nullptr);
        if (thread != nullptr) CloseHandle(thread);
    }
}

bool EnsureInstalled(HWND window) {
    if (g_window == window && window != nullptr) {
        return !g_settings.rawMouseInput || raw_input::AttachWindow(window);
    }
    if (window == nullptr || !IsWindow(window)) return false;
    if (InterlockedCompareExchange(&g_installing, 1, 0) != 0) return false;
    SetLastError(0);
    const LONG_PTR previous = SetWindowLongPtrW(window, GWLP_WNDPROC,
                                                reinterpret_cast<LONG_PTR>(SharedWndProc));
    if (previous == 0 && GetLastError() != 0) {
        InterlockedExchange(&g_installing, 0);
        return false;
    }
    g_window = window;
    g_originalWndProc = reinterpret_cast<WNDPROC>(previous);
    g_activationState = ActivationState::Unknown;
    borderless_fullscreen::Apply(window);
    if (g_settings.borderlessFullscreen) {
        g_borderlessVerificationTicks = 0;
        SetTimer(window, kBorderlessVerificationTimer, 250, nullptr);
    }
    if (g_settings.borderlessGamma) {
        SetTimer(window, kBorderlessGammaTimer, 250, nullptr);
    }
    SetTimer(window, kForegroundVerificationTimer, 100, nullptr);
    cursor_clip::Initialize(g_settings.restoreCursorClip, window);
    if (g_settings.rawMouseInput) {
        if (!raw_input::AttachWindow(window)) {
            g_settings.rawMouseInput = false;
        } else {
            PostMessageW(window, kInitialFocusRecoveryMessage, 0, 0);
        }
    }
    UpdateForegroundState();
    InterlockedExchange(&g_installing, 0);
    return true;
}

}  // namespace game_window
