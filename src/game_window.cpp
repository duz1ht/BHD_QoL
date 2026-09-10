#include "game_window.h"

#include <cstdint>

#include "cursor_clip.h"
#include "fullscreen_borderless.h"
#include "logger.h"
#include "raw_input.h"

namespace game_window {
namespace {
Settings g_settings = {};
HWND g_window = nullptr;
WNDPROC g_originalWndProc = nullptr;
volatile LONG g_installing = 0;
constexpr uintptr_t kGameWindow = 0x00F654FC;

void TryResume(const char* trigger) {
    const bool clipReady = cursor_clip::HandleFocusGained(trigger);
    if (g_settings.rawMouseInput && clipReady) {
        raw_input::HandleFocusGained(trigger, true);
    }
}

void ResumeRawInputIfReady(const char* trigger) {
    if (g_settings.rawMouseInput && cursor_clip::IsReady()) {
        raw_input::HandleFocusGained(trigger, true);
    }
}

void HandleFocusLost() {
    if (g_settings.rawMouseInput) raw_input::HandleFocusLost();
    cursor_clip::HandleFocusLost();
}

LRESULT CALLBACK SharedWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INPUT && g_settings.rawMouseInput) {
        raw_input::HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        return DefWindowProcW(window, message, wParam, lParam);
    }
    const LRESULT result = CallWindowProcW(g_originalWndProc, window, message, wParam, lParam);
    switch (message) {
        case WM_ACTIVATEAPP:
            if (wParam) TryResume("WM_ACTIVATEAPP");
            else HandleFocusLost();
            break;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) HandleFocusLost();
            else TryResume("WM_ACTIVATE");
            break;
        case WM_SETFOCUS:
            TryResume("WM_SETFOCUS");
            break;
        case WM_KILLFOCUS:
            HandleFocusLost();
            break;
        case WM_MOVE:
        case WM_DISPLAYCHANGE:
            fullscreen_borderless::Apply(window,
                message == WM_MOVE ? "WM_MOVE" : "WM_DISPLAYCHANGE");
            cursor_clip::HandleWindowChanged(message == WM_MOVE ? "WM_MOVE" : "WM_DISPLAYCHANGE");
            ResumeRawInputIfReady(message == WM_MOVE ? "WM_MOVE" : "WM_DISPLAYCHANGE");
            break;
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) fullscreen_borderless::Apply(window, "WM_SIZE");
            cursor_clip::HandleWindowChanged(wParam == SIZE_MINIMIZED ? "WM_SIZE_MINIMIZED" : "WM_SIZE");
            if (wParam != SIZE_MINIMIZED) ResumeRawInputIfReady("WM_SIZE");
            break;
        case WM_DESTROY:
            HandleFocusLost();
            break;
        case WM_NCDESTROY:
            raw_input::HandleDestroy();
            cursor_clip::Shutdown();
            g_window = nullptr;
            g_originalWndProc = nullptr;
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
                "configured RawMouseInput=%d RestoreCursorClip=%d FullscreenBorderless=%d",
                settings.rawMouseInput, settings.restoreCursorClip, settings.fullscreenBorderless);
    if ((!settings.rawMouseInput && settings.restoreCursorClip) || settings.fullscreenBorderless) {
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
    logger::Log("INFO", "GameWindow", "shared WndProc installed hwnd=0x%08lX",
                reinterpret_cast<unsigned long>(window));
    fullscreen_borderless::Apply(window, "initialization");
    cursor_clip::Initialize(g_settings.restoreCursorClip, window);
    if (g_settings.rawMouseInput && !raw_input::AttachWindow(window)) {
        logger::Log("ERROR", "GameWindow",
                    "Raw Input window attachment failed; legacy mouse retained and "
                    "RestoreCursorClip remains available");
        g_settings.rawMouseInput = false;
    }
    TryResume("initialization");
    InterlockedExchange(&g_installing, 0);
    return true;
}

void HandleStatisticsInterval() {
    cursor_clip::HandleWindowChanged("statistics_interval");
}

}  // namespace game_window
