#include "game_window.h"

#include <cstdint>

#include "cursor_clip.h"
#include "cursor_visibility.h"
#include "borderless_fullscreen.h"
#include "borderless_gamma.h"
#include "logger.h"
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

const char* ActivationStateName(ActivationState state) {
    switch (state) {
        case ActivationState::Unknown: return "unknown";
        case ActivationState::Inactive: return "inactive";
        case ActivationState::ActivationPending: return "activation_pending";
        case ActivationState::Active: return "active";
    }
    return "invalid";
}

bool IsGameForeground(HWND* foregroundResult = nullptr, HWND* rootResult = nullptr,
                      DWORD* processResult = nullptr) {
    const HWND foreground = GetForegroundWindow();
    const HWND root = foreground == nullptr ? nullptr : GetAncestor(foreground, GA_ROOT);
    DWORD process = 0;
    if (foreground != nullptr) GetWindowThreadProcessId(foreground, &process);
    if (foregroundResult != nullptr) *foregroundResult = foreground;
    if (rootResult != nullptr) *rootResult = root;
    if (processResult != nullptr) *processResult = process;
    return foreground != nullptr && !IsIconic(g_window) &&
           (root == g_window || process == GetCurrentProcessId());
}

bool WindowHasInputFocus() {
    const HWND focus = GetFocus();
    return GetForegroundWindow() == g_window &&
           (focus == g_window || (focus != nullptr && GetAncestor(focus, GA_ROOT) == g_window));
}

bool TryResume(const char* trigger) {
    if (!WindowHasInputFocus()) {
        logger::Log("INFO", "GameWindow",
                    "activation=deferred trigger=%s foreground=0x%08lX focus=0x%08lX",
                    trigger, reinterpret_cast<unsigned long>(GetForegroundWindow()),
                    reinterpret_cast<unsigned long>(GetFocus()));
        return false;
    }
    const bool clipReady = cursor_clip::HandleFocusGained(trigger);
    if (!clipReady) return false;
    if (g_settings.rawMouseInput && !raw_input::HandleFocusGained(trigger, true)) return false;
    cursor_visibility::HandleGameActivated(trigger);
    if (g_settings.borderlessGamma) borderless_gamma::HandleFocusGained(g_window, trigger);
    return true;
}

void HandleFocusLost(const char* trigger) {
    if (g_settings.borderlessGamma) borderless_gamma::HandleFocusLost(trigger);
    mouse_scaling_fix::Reset();
    if (g_settings.rawMouseInput) raw_input::HandleFocusLost();
    cursor_clip::HandleFocusLost();
    cursor_visibility::HandleGameDeactivated(trigger);
}

void UpdateForegroundState(const char* trigger) {
    HWND foreground = nullptr;
    HWND root = nullptr;
    DWORD process = 0;
    const bool foregroundIsGame = IsGameForeground(&foreground, &root, &process);
    if (!foregroundIsGame) {
        if (g_activationState == ActivationState::Inactive) return;
        const ActivationState previous = g_activationState;
        g_activationState = ActivationState::Inactive;
        logger::Log("INFO", "GameWindow",
                    "trigger=%s activation=%s->inactive foreground=0x%08lX root=0x%08lX "
                    "process=%lu",
                    trigger, ActivationStateName(previous),
                    reinterpret_cast<unsigned long>(foreground),
                    reinterpret_cast<unsigned long>(root), process);
        HandleFocusLost(trigger);
        return;
    }

    if (g_activationState == ActivationState::Active) return;
    if (g_activationState != ActivationState::ActivationPending) {
        const ActivationState previous = g_activationState;
        g_activationState = ActivationState::ActivationPending;
        logger::Log("INFO", "GameWindow",
                    "trigger=%s activation=%s->activation_pending foreground=0x%08lX "
                    "root=0x%08lX process=%lu",
                    trigger, ActivationStateName(previous),
                    reinterpret_cast<unsigned long>(foreground),
                    reinterpret_cast<unsigned long>(root), process);
    }
    if (TryResume(trigger)) {
        g_activationState = ActivationState::Active;
        logger::Log("INFO", "GameWindow", "trigger=%s activation=completed", trigger);
    }
}

void HandleInitialFocusRecovery(HWND window) {
    if (!g_settings.rawMouseInput || !IsWindowVisible(window) || IsIconic(window) ||
        GetForegroundWindow() != window) {
        return;
    }
    const HWND focusBefore = GetFocus();
    if (focusBefore == nullptr) {
        SetLastError(ERROR_SUCCESS);
        const HWND previousFocus = SetFocus(window);
        const DWORD error = GetFocus() == window ? ERROR_SUCCESS : GetLastError();
        logger::Log(error == ERROR_SUCCESS ? "INFO" : "WARN", "GameWindow",
                    "initial focus recovery previous=0x%08lX current=0x%08lX error=%lu",
                    reinterpret_cast<unsigned long>(previousFocus),
                    reinterpret_cast<unsigned long>(GetFocus()), error);
    }
    UpdateForegroundState("initial_focus_recovery");
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
            UpdateForegroundState("WM_ACTIVATEAPP");
            break;
        case WM_ACTIVATE:
            UpdateForegroundState("WM_ACTIVATE");
            break;
        case WM_SETFOCUS:
            UpdateForegroundState("WM_SETFOCUS");
            break;
        case WM_KILLFOCUS:
            UpdateForegroundState("WM_KILLFOCUS");
            break;
        case WM_MOVE:
        case WM_DISPLAYCHANGE:
            borderless_fullscreen::Apply(window,
                message == WM_MOVE ? "WM_MOVE" : "WM_DISPLAYCHANGE");
            cursor_clip::HandleWindowChanged(message == WM_MOVE ? "WM_MOVE" : "WM_DISPLAYCHANGE");
            UpdateForegroundState(message == WM_MOVE ? "WM_MOVE" : "WM_DISPLAYCHANGE");
            if (message == WM_DISPLAYCHANGE && g_settings.borderlessGamma &&
                g_activationState == ActivationState::Active)
                borderless_gamma::HandleDisplayChanged(window, "WM_DISPLAYCHANGE");
            break;
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) borderless_fullscreen::Apply(window, "WM_SIZE");
            cursor_clip::HandleWindowChanged(wParam == SIZE_MINIMIZED ? "WM_SIZE_MINIMIZED" : "WM_SIZE");
            UpdateForegroundState(wParam == SIZE_MINIMIZED ? "WM_SIZE_MINIMIZED" : "WM_SIZE");
            break;
        case WM_TIMER:
            if (wParam == kForegroundVerificationTimer)
                UpdateForegroundState("foreground_timer");
            if (wParam == kBorderlessGammaTimer &&
                g_activationState == ActivationState::Active) borderless_gamma::Poll(window);
            if (wParam == kBorderlessVerificationTimer) {
                borderless_fullscreen::Apply(window, "verification_timer");
                if (++g_borderlessVerificationTicks >= 20) {
                    KillTimer(window, kBorderlessVerificationTimer);
                }
            }
            break;
        case WM_DESTROY:
            if (g_activationState != ActivationState::Inactive) {
                g_activationState = ActivationState::Inactive;
                HandleFocusLost("WM_DESTROY");
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
    logger::Log("ERROR", "GameWindow", "game window was not available within 30 seconds");
    return 1;
}
}  // namespace

void Configure(const Settings& settings) {
    g_settings = settings;
    logger::Log("INFO", "GameWindow",
                "configured RawMouseInput=%d RestoreCursorClip=%d BorderlessFullscreen=%d "
                "BorderlessGamma=%d MouseScalingFix=%d",
                settings.rawMouseInput, settings.restoreCursorClip, settings.borderlessFullscreen,
                settings.borderlessGamma, settings.mouseScalingFix);
    if ((!settings.rawMouseInput && (settings.restoreCursorClip || settings.mouseScalingFix)) ||
        settings.borderlessFullscreen) {
        HANDLE thread = CreateThread(nullptr, 0, WindowDiscoveryThread, nullptr, 0, nullptr);
        if (thread != nullptr) CloseHandle(thread);
        else logger::Log("ERROR", "GameWindow", "window discovery thread failed: error=%lu",
                         GetLastError());
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
    logger::Log("INFO", "GameWindow", "shared WndProc installed hwnd=0x%08lX",
                reinterpret_cast<unsigned long>(window));
    borderless_fullscreen::Apply(window, "initialization");
    if (g_settings.borderlessFullscreen) {
        g_borderlessVerificationTicks = 0;
        if (SetTimer(window, kBorderlessVerificationTimer, 250, nullptr) == 0) {
            logger::Log("WARN", "GameWindow",
                        "could not start borderless verification timer: error=%lu",
                        GetLastError());
        }
    }
    if (g_settings.borderlessGamma && SetTimer(window, kBorderlessGammaTimer, 250, nullptr) == 0) {
        logger::Log("WARN", "GameWindow", "could not start gamma timer: error=%lu",
                    GetLastError());
    }
    if (SetTimer(window, kForegroundVerificationTimer, 100, nullptr) == 0) {
        logger::Log("WARN", "GameWindow",
                    "could not start foreground verification timer: error=%lu", GetLastError());
    }
    cursor_clip::Initialize(g_settings.restoreCursorClip, window);
    if (g_settings.rawMouseInput) {
        if (!raw_input::AttachWindow(window)) {
            logger::Log("ERROR", "GameWindow",
                        "Raw Input window attachment failed; legacy mouse retained and "
                        "RestoreCursorClip remains available");
            g_settings.rawMouseInput = false;
        } else if (!PostMessageW(window, kInitialFocusRecoveryMessage, 0, 0)) {
            logger::Log("WARN", "GameWindow",
                        "could not post initial focus recovery: error=%lu", GetLastError());
        }
    }
    UpdateForegroundState("initialization");
    InterlockedExchange(&g_installing, 0);
    return true;
}

void HandleStatisticsInterval() {
    cursor_clip::HandleWindowChanged("statistics_interval");
}

}  // namespace game_window
