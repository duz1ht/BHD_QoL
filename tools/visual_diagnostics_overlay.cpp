#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cwchar>

#include "visual_diagnostics.h"

namespace {
constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT kRefreshMs = 100;
constexpr int kOverlayWidth = 460;
constexpr int kOverlayHeight = 382;

HANDLE g_mapping = nullptr;
const visual_diagnostics::SharedTelemetry* g_shared = nullptr;
visual_diagnostics::SharedTelemetry g_previous = {};
ULONGLONG g_previousTick = 0;
bool g_userVisible = true;

struct Rates {
    double rawReports = 0;
    double logicPolls = 0;
    double renderPolls = 0;
    double cameraEvaluations = 0;
    double cameraFrames = 0;
    double visualYawChanges = 0;
    double officialYawChanges = 0;
} g_rates;

LONG Load(const volatile LONG* value) {
    return *value;
}

uint32_t Delta(LONG current, LONG previous) {
    return static_cast<uint32_t>(current) - static_cast<uint32_t>(previous);
}

bool Connect() {
    if (g_shared != nullptr) return true;
    g_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, visual_diagnostics::kMappingName);
    if (g_mapping == nullptr) return false;
    g_shared = static_cast<const visual_diagnostics::SharedTelemetry*>(
        MapViewOfFile(g_mapping, FILE_MAP_READ, 0, 0,
                      sizeof(visual_diagnostics::SharedTelemetry)));
    if (g_shared == nullptr) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        return false;
    }
    if (g_shared->magic != visual_diagnostics::kMagic ||
        g_shared->version != visual_diagnostics::kVersion) {
        UnmapViewOfFile(g_shared);
        CloseHandle(g_mapping);
        g_shared = nullptr;
        g_mapping = nullptr;
        return false;
    }
    g_previous = *g_shared;
    g_previousTick = GetTickCount64();
    return true;
}

void Disconnect() {
    if (g_shared != nullptr) UnmapViewOfFile(g_shared);
    if (g_mapping != nullptr) CloseHandle(g_mapping);
    g_shared = nullptr;
    g_mapping = nullptr;
    g_previousTick = 0;
    g_rates = {};
}

void UpdateRates() {
    if (!Connect()) return;
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, g_shared->processId);
    if (process != nullptr && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
        CloseHandle(process);
        Disconnect();
        return;
    }
    if (process != nullptr) CloseHandle(process);

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG elapsedMs = now - g_previousTick;
    if (elapsedMs < 250) return;
    const double factor = 1000.0 / static_cast<double>(elapsedMs);
#define RATE(field) Delta(Load(&g_shared->field), Load(&g_previous.field)) * factor
    g_rates.rawReports = RATE(rawReports);
    g_rates.logicPolls = RATE(logicPolls);
    g_rates.renderPolls = RATE(renderPolls);
    g_rates.cameraEvaluations = RATE(cameraEvaluations);
    g_rates.cameraFrames = RATE(cameraFrames);
    g_rates.visualYawChanges = RATE(visualYawChanges);
    g_rates.officialYawChanges = RATE(officialYawChanges);
#undef RATE
    g_previous = *g_shared;
    g_previousTick = now;
}

void FollowGameWindow(HWND overlay) {
    if (!g_userVisible) return;
    if (g_shared == nullptr) return;
    const HWND game = reinterpret_cast<HWND>(static_cast<uintptr_t>(g_shared->gameWindow));
    if (!IsWindow(game) || IsIconic(game)) {
        ShowWindow(overlay, SW_HIDE);
        return;
    }
    RECT client = {};
    POINT origin = {};
    if (!GetClientRect(game, &client) || !ClientToScreen(game, &origin)) return;
    SetWindowPos(overlay, HWND_TOPMOST, origin.x + 16, origin.y + 16,
                 kOverlayWidth, kOverlayHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void DrawLine(HDC dc, int y, COLORREF color, const wchar_t* label, double rate,
              const wchar_t* suffix = L"Hz") {
    wchar_t text[160] = {};
    std::swprintf(text, sizeof(text) / sizeof(text[0]), L"%-28ls %7.1f %ls", label, rate, suffix);
    SetTextColor(dc, color);
    TextOutW(dc, 16, y, text, lstrlenW(text));
}

const wchar_t* SupportReason(LONG value) {
    using Reason = visual_diagnostics::CameraSupportReason;
    switch (static_cast<Reason>(value)) {
        case Reason::Supported: return L"SUPPORTED";
        case Reason::FeatureDisabled: return L"HighRateCameraRotation disabled";
        case Reason::RawInputInactive: return L"Raw Input inactive";
        case Reason::CameraMode: return L"camera mode is not normal first person";
        case Reason::Paused: return L"pause/loading state is active";
        case Reason::MouseDisabled: return L"game mouse input is disabled";
        case Reason::InputObjectMissing: return L"game input object is null";
        case Reason::InputReadyMissing: return L"game input-ready state is null";
        case Reason::LocalPlayerMissing: return L"local-player pointer is null";
        case Reason::CameraOwnerMismatch: return L"camera owner is not local player";
        case Reason::MountedOrVehicle: return L"ride/mount target is active";
        default: return L"unknown guard failure";
    }
}

void Paint(HWND window) {
    PAINTSTRUCT paint = {};
    HDC dc = BeginPaint(window, &paint);
    RECT area = {};
    GetClientRect(window, &area);
    HBRUSH background = CreateSolidBrush(RGB(12, 16, 20));
    FillRect(dc, &area, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);
    HFONT font = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    const HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, RGB(120, 210, 255));
    const wchar_t title[] = L"BHD QoL - Visual Pipeline";
    TextOutW(dc, 16, 12, title, static_cast<int>(sizeof(title) / sizeof(title[0]) - 1));

    if (g_shared == nullptr) {
        SetTextColor(dc, RGB(255, 190, 80));
        const wchar_t waiting[] = L"Aguardando dfbhd.exe + dinput8.dll...";
        TextOutW(dc, 16, 52, waiting, static_cast<int>(sizeof(waiting) / sizeof(waiting[0]) - 1));
    } else {
        const COLORREF good = RGB(100, 240, 150);
        const COLORREF normal = RGB(225, 230, 235);
        DrawLine(dc, 44, normal, L"Raw Input reports", g_rates.rawReports);
        DrawLine(dc, 68, normal, L"Logic PollMouseInput", g_rates.logicPolls);
        DrawLine(dc, 92, good, L"Camera call-site / render poll", g_rates.renderPolls);
        DrawLine(dc, 116, normal, L"Camera evaluator calls", g_rates.cameraEvaluations);
        DrawLine(dc, 140, good, L"Visual camera applied", g_rates.cameraFrames);
        DrawLine(dc, 164, good, L"Visual yaw changes", g_rates.visualYawChanges);
        DrawLine(dc, 188, normal, L"Official yaw changes", g_rates.officialYawChanges);

        wchar_t counts[180] = {};
        std::swprintf(counts, sizeof(counts) / sizeof(counts[0]),
                      L"Pending raw: X=%ld Y=%ld   newest: X=%ld Y=%ld",
                      Load(&g_shared->pendingX), Load(&g_shared->pendingY),
                      Load(&g_shared->lastVisualDeltaX), Load(&g_shared->lastVisualDeltaY));
        SetTextColor(dc, normal);
        TextOutW(dc, 16, 218, counts, lstrlenW(counts));

        wchar_t guards[220] = {};
        std::swprintf(guards, sizeof(guards) / sizeof(guards[0]),
                      L"mode=%ld pause=%ld mouse=%ld input=%08lX ready=%08lX",
                      Load(&g_shared->cameraMode), Load(&g_shared->pauseState),
                      Load(&g_shared->mouseEnabled), Load(&g_shared->inputObject),
                      Load(&g_shared->inputReady));
        SetTextColor(dc, RGB(155, 165, 175));
        TextOutW(dc, 16, 246, guards, lstrlenW(guards));
        std::swprintf(guards, sizeof(guards) / sizeof(guards[0]),
                      L"local=%08lX owner=%08lX ride=%08lX",
                      Load(&g_shared->localPlayer), Load(&g_shared->cameraOwner),
                      Load(&g_shared->rideTarget));
        TextOutW(dc, 16, 270, guards, lstrlenW(guards));

        wchar_t reason[220] = {};
        std::swprintf(reason, sizeof(reason) / sizeof(reason[0]), L"Guard: %ls",
                      SupportReason(Load(&g_shared->cameraSupportReason)));
        SetTextColor(dc, Load(&g_shared->cameraValid) ? good : RGB(255, 190, 80));
        TextOutW(dc, 16, 294, reason, lstrlenW(reason));

        const bool moving = g_rates.rawReports > 10.0;
        const bool renderRate = g_rates.cameraFrames > g_rates.logicPolls * 1.5;
        const bool visualFaster = g_rates.visualYawChanges > g_rates.officialYawChanges * 1.35;
        const wchar_t* status = !Load(&g_shared->rawActive) ? L"RAW INPUT INATIVO"
            : !Load(&g_shared->cameraValid) ? L"CAMERA NATIVA / ESTADO NAO SUPORTADO"
            : !moving ? L"PRONTO - MOVA O MOUSE PARA MEDIR"
            : renderRate && visualFaster ? L"CAMERA VISUAL LIVRE PELO FPS DO RENDER"
            : L"ATENCAO: CAMERA AINDA PARECE LIMITADA PELA LOGICA";
        SetTextColor(dc, renderRate && visualFaster ? good : RGB(255, 190, 80));
        TextOutW(dc, 16, 322, status, lstrlenW(status));
        SetTextColor(dc, RGB(155, 165, 175));
        const wchar_t hint[] = L"F8 mostra/oculta | F9 fecha";
        TextOutW(dc, 16, 350, hint, static_cast<int>(sizeof(hint) / sizeof(hint[0]) - 1));
    }
    SelectObject(dc, oldFont);
    DeleteObject(font);
    EndPaint(window, &paint);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_TIMER:
            UpdateRates();
            FollowGameWindow(window);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_PAINT: Paint(window); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProcW(window, message, wParam, lParam);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const wchar_t className[] = L"BHDQoLVisualDiagnosticsOverlay";
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = className;
    if (!RegisterClassW(&windowClass)) return 1;

    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT |
                                      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                  className, L"BHD QoL Visual Diagnostics", WS_POPUP,
                                  16, 16, kOverlayWidth, kOverlayHeight,
                                  nullptr, nullptr, instance, nullptr);
    if (window == nullptr) return 2;
    SetLayeredWindowAttributes(window, 0, 225, LWA_ALPHA);
    RegisterHotKey(window, 1, 0, VK_F8);
    RegisterHotKey(window, 2, 0, VK_F9);
    SetTimer(window, kRefreshTimer, kRefreshMs, nullptr);
    ShowWindow(window, SW_SHOWNOACTIVATE);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_HOTKEY && message.wParam == 1) {
            g_userVisible = !g_userVisible;
            ShowWindow(window, g_userVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
            continue;
        }
        if (message.message == WM_HOTKEY && message.wParam == 2) break;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    UnregisterHotKey(window, 1);
    UnregisterHotKey(window, 2);
    Disconnect();
    DestroyWindow(window);
    return 0;
}
