#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>

#include <algorithm>
#include <array>

#include "player_config_resource.h"

namespace {
constexpr wchar_t kWindowClass[] = L"BhdPlayerConfigWindow";
constexpr wchar_t kIniName[] = L"bhd_player_config.ini";
constexpr wchar_t kSection[] = L"PatchGroups";
constexpr int kTabId = 100;
constexpr int kSaveId = 101;
constexpr int kReloadId = 102;
constexpr int kDefaultsId = 103;
constexpr int kFirstSettingId = 1000;
constexpr int kFirstRecommendationId = 3000;

struct Setting {
    const wchar_t* key;
    const wchar_t* title;
    const wchar_t* description;
    bool defaultValue;
    bool recommended;
};

constexpr std::array<Setting, 14> kSettings{{
    {L"BorderlessFullscreen", L"Borderless fullscreen", L"Runs the game in a borderless window covering the monitor.", true, false},
    {L"ForceDesktopResolution", L"Force desktop resolution", L"Uses the monitor resolution while borderless mode is enabled.", true, false},
    {L"UseCorrectAspectFOV", L"Correct aspect-ratio FOV", L"Corrects first-person FOV for widescreen render resolutions.", true, true},
    {L"DPIAware", L"DPI awareness", L"Keeps window, monitor and cursor coordinates on the same scale.", true, true},
    {L"AdaptiveScreenCenter", L"Adaptive screen center", L"Calculates screen center from the current resolution.", true, true},
    {L"RawMouseInput", L"Raw mouse input", L"Uses Windows Raw Input for relative mouse movement.", true, true},
    {L"MouseScalingFix", L"Mouse scaling fix", L"Corrects fixed-point rounding, especially while scoped.", true, true},
    {L"MouseScaleQueryFix", L"Mouse scale query fix", L"Makes a bare mousescale command report without changing it.", true, true},
    {L"ScaleCursorClipToResolution", L"Scale cursor confinement", L"Uses the current resolution for cursor confinement bounds.", true, true},
    {L"RestoreCursorClip", L"Restore cursor confinement", L"Restores confinement after focus, display or window changes.", true, true},
    {L"HighFrequencyCamera", L"High-frequency camera rotation", L"Applies recent Raw Input to the presentation camera.", true, false},
    {L"HighFrequencyCameraPosition", L"High-frequency camera position", L"Smooths first-person camera movement between game updates.", true, false},
    {L"HighFrequencyViewmodel", L"High-frequency viewmodel", L"Makes first-person hands and weapons follow the smooth camera.", true, false},
    {L"AllowHighResolutionFonts", L"Allow high-resolution fonts", L"Loads compatible FNT0 atlases from 256 through 4096 pixels.", true, true},
}};

struct SettingCategory {
    const wchar_t* title;
    size_t firstSetting;
    size_t settingCount;
};

constexpr std::array<SettingCategory, 4> kCategories{{
    {L"Display", 0, 5},
    {L"Mouse && Cursor", 5, 5},
    {L"Camera && Viewmodel", 10, 3},
    {L"Fonts", 13, 1},
}};

constexpr int kCategoryTopPadding = 26;
constexpr int kSettingRowHeight = 54;
constexpr int kCategoryBottomPadding = 10;
constexpr int kCategoryGap = 12;

HWND gTab = nullptr;
HWND gSettingsPage = nullptr;
HWND gPlaceholderPage = nullptr;
HWND gStatus = nullptr;
HFONT gFont = nullptr;
HFONT gBoldFont = nullptr;
wchar_t gIniPath[MAX_PATH] = {};
std::array<HWND, kSettings.size()> gSettingCheckboxes{};
std::array<HWND, kSettings.size()> gSettingDescriptions{};
std::array<HWND, kSettings.size()> gRecommendationLabels{};
std::array<int, kSettings.size()> gSettingTitleWidths{};
std::array<HWND, kCategories.size()> gCategoryBoxes{};
int gSettingsScrollPosition = 0;

void ApplyFont(HWND control) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
}

HWND MakeControl(HWND parent, const wchar_t* className, const wchar_t* text, DWORD style,
                 int x, int y, int width, int height, int id = 0) {
    HWND control = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style,
                                   x, y, width, height, parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    ApplyFont(control);
    return control;
}

void BuildIniPath() {
    const DWORD length = GetModuleFileNameW(nullptr, gIniPath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        lstrcpynW(gIniPath, L".\\bhd_player_config.ini", MAX_PATH);
        return;
    }
    PathRemoveFileSpecW(gIniPath);
    PathAppendW(gIniPath, kIniName);
}

void SetStatus(const wchar_t* text) {
    SetWindowTextW(gStatus, text);
}

void LoadSettings(bool defaultsOnly) {
    for (size_t index = 0; index < kSettings.size(); ++index) {
        const auto& setting = kSettings[index];
        const bool enabled = defaultsOnly
            ? setting.defaultValue
            : GetPrivateProfileIntW(kSection, setting.key, setting.defaultValue ? 1 : 0, gIniPath) != 0;
        CheckDlgButton(gSettingsPage, kFirstSettingId + static_cast<int>(index),
                       enabled ? BST_CHECKED : BST_UNCHECKED);
    }
    SetStatus(defaultsOnly ? L"Default values loaded. Click Save to write them."
                           : L"Settings loaded from bhd_player_config.ini.");
}

void SaveSettings(HWND owner) {
    bool saved = true;
    for (size_t index = 0; index < kSettings.size(); ++index) {
        const bool enabled = IsDlgButtonChecked(gSettingsPage,
            kFirstSettingId + static_cast<int>(index)) == BST_CHECKED;
        saved = WritePrivateProfileStringW(kSection, kSettings[index].key,
                                           enabled ? L"1" : L"0", gIniPath) != FALSE && saved;
    }
    if (saved) {
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, gIniPath);
        SetStatus(L"Saved. Restart the game to apply changes.");
    } else {
        SetStatus(L"Could not save the configuration file.");
        MessageBoxW(owner,
                    L"BHD Player Config could not write bhd_player_config.ini.\n\n"
                    L"Check that this folder is writable, then try again.",
                    L"Save failed", MB_OK | MB_ICONERROR);
    }
}

void ShowSelectedTab() {
    const int selected = TabCtrl_GetCurSel(gTab);
    ShowWindow(gSettingsPage, selected == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(gPlaceholderPage, selected == 0 ? SW_HIDE : SW_SHOW);
    if (selected > 0) {
        wchar_t message[256] = {};
        const wchar_t* name = selected == 1 ? L"Crosshair" : selected == 2 ? L"HUD" : L"Fonts";
        wsprintfW(message, L"%s settings will appear here when those features are added.", name);
        SetWindowTextW(GetDlgItem(gPlaceholderPage, 200), message);
    }
}

void Layout(HWND window) {
    RECT client{};
    GetClientRect(window, &client);
    const int margin = 16;
    const int buttonWidth = 92;
    const int buttonHeight = 30;
    const int bottomY = client.bottom - margin - buttonHeight;
    MoveWindow(gTab, margin, margin, client.right - margin * 2, bottomY - margin * 2, TRUE);

    RECT pageRect{};
    GetClientRect(gTab, &pageRect);
    TabCtrl_AdjustRect(gTab, FALSE, &pageRect);
    MapWindowPoints(gTab, window, reinterpret_cast<POINT*>(&pageRect), 2);
    MoveWindow(gSettingsPage, pageRect.left, pageRect.top,
               pageRect.right - pageRect.left, pageRect.bottom - pageRect.top, TRUE);
    MoveWindow(gPlaceholderPage, pageRect.left, pageRect.top,
               pageRect.right - pageRect.left, pageRect.bottom - pageRect.top, TRUE);

    MoveWindow(GetDlgItem(window, kSaveId), client.right - margin - buttonWidth, bottomY,
               buttonWidth, buttonHeight, TRUE);
    MoveWindow(GetDlgItem(window, kReloadId), client.right - margin * 2 - buttonWidth * 2,
               bottomY, buttonWidth, buttonHeight, TRUE);
    MoveWindow(GetDlgItem(window, kDefaultsId), client.right - margin * 3 - buttonWidth * 3,
               bottomY, buttonWidth, buttonHeight, TRUE);
    MoveWindow(gStatus, margin, bottomY + 6,
               client.right - margin * 5 - buttonWidth * 3, 22, TRUE);
}

int SettingsContentHeight() {
    int height = kCategoryGap;
    for (const auto& category : kCategories) {
        height += kCategoryTopPadding
            + static_cast<int>(category.settingCount) * kSettingRowHeight
            + kCategoryBottomPadding + kCategoryGap;
    }
    return height;
}

void LayoutSettingsControls() {
    if (!gSettingsPage) return;

    RECT client{};
    GetClientRect(gSettingsPage, &client);
    const int contentWidth = client.right - client.left;
    int categoryY = kCategoryGap - gSettingsScrollPosition;

    for (size_t categoryIndex = 0; categoryIndex < kCategories.size(); ++categoryIndex) {
        const auto& category = kCategories[categoryIndex];
        const int categoryHeight = kCategoryTopPadding
            + static_cast<int>(category.settingCount) * kSettingRowHeight
            + kCategoryBottomPadding;
        MoveWindow(gCategoryBoxes[categoryIndex], 8, categoryY,
                   contentWidth > 16 ? contentWidth - 16 : 0, categoryHeight, TRUE);

        for (size_t item = 0; item < category.settingCount; ++item) {
            const size_t settingIndex = category.firstSetting + item;
            const int settingY = categoryY + kCategoryTopPadding
                + static_cast<int>(item) * kSettingRowHeight;
            const int checkboxWidth = std::min(
                gSettingTitleWidths[settingIndex] + 22,
                contentWidth > 48 ? contentWidth - 48 : 0);
            MoveWindow(gSettingCheckboxes[settingIndex], 24, settingY,
                       checkboxWidth, 21, TRUE);
            if (gRecommendationLabels[settingIndex]) {
                const int recommendationX = 24 + checkboxWidth + 5;
                MoveWindow(gRecommendationLabels[settingIndex], recommendationX, settingY + 2,
                           contentWidth > recommendationX + 8
                               ? contentWidth - recommendationX - 8 : 0,
                           19, TRUE);
            }
            MoveWindow(gSettingDescriptions[settingIndex], 46, settingY + 22,
                       contentWidth > 70 ? contentWidth - 70 : 0, 28, TRUE);
        }
        categoryY += categoryHeight + kCategoryGap;
    }
}

void SetSettingsScrollPosition(int requestedPosition) {
    RECT client{};
    GetClientRect(gSettingsPage, &client);
    const int clientHeight = static_cast<int>(client.bottom - client.top);
    const int maximum = std::max(0, SettingsContentHeight() - clientHeight);
    gSettingsScrollPosition = std::max(0, std::min(requestedPosition, maximum));

    SCROLLINFO info{sizeof(info)};
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = SettingsContentHeight() - 1;
    info.nPage = static_cast<UINT>(std::max(0, clientHeight));
    info.nPos = gSettingsScrollPosition;
    SetScrollInfo(gSettingsPage, SB_VERT, &info, TRUE);
    LayoutSettingsControls();
    RedrawWindow(gSettingsPage, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

LRESULT CALLBACK SettingsPageProc(HWND page, UINT message, WPARAM wParam, LPARAM lParam,
                                  UINT_PTR, DWORD_PTR) {
    switch (message) {
        case WM_SIZE:
            SetSettingsScrollPosition(gSettingsScrollPosition);
            return 0;
        case WM_VSCROLL: {
            int position = gSettingsScrollPosition;
            RECT client{};
            GetClientRect(page, &client);
            const int clientHeight = static_cast<int>(client.bottom - client.top);
            switch (LOWORD(wParam)) {
                case SB_LINEUP: position -= 32; break;
                case SB_LINEDOWN: position += 32; break;
                case SB_PAGEUP: position -= std::max(32, clientHeight - 32); break;
                case SB_PAGEDOWN: position += std::max(32, clientHeight - 32); break;
                case SB_THUMBPOSITION:
                case SB_THUMBTRACK: {
                    SCROLLINFO info{sizeof(info)};
                    info.fMask = SIF_TRACKPOS;
                    GetScrollInfo(page, SB_VERT, &info);
                    position = info.nTrackPos;
                    break;
                }
                case SB_TOP: position = 0; break;
                case SB_BOTTOM: position = SettingsContentHeight(); break;
                default: return 0;
            }
            SetSettingsScrollPosition(position);
            return 0;
        }
        case WM_MOUSEWHEEL:
            SetSettingsScrollPosition(gSettingsScrollPosition
                - GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 96);
            return 0;
        case WM_CTLCOLORSTATIC: {
            const int controlId = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
            if (controlId >= kFirstRecommendationId &&
                controlId < kFirstRecommendationId + static_cast<int>(kSettings.size())) {
                HDC deviceContext = reinterpret_cast<HDC>(wParam);
                const size_t settingIndex = static_cast<size_t>(controlId - kFirstRecommendationId);
                SetTextColor(deviceContext,
                             kSettings[settingIndex].recommended
                                 ? RGB(0, 128, 0)
                                 : RGB(96, 96, 96));
                SetBkColor(deviceContext, GetSysColor(COLOR_BTNFACE));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            }
            break;
        }
    }
    return DefSubclassProc(page, message, wParam, lParam);
}

void CreateSettingsControls(HWND page) {
    SetWindowSubclass(page, SettingsPageProc, 1, 0);
    for (size_t categoryIndex = 0; categoryIndex < kCategories.size(); ++categoryIndex) {
        gCategoryBoxes[categoryIndex] = MakeControl(
            page, L"BUTTON", kCategories[categoryIndex].title, BS_GROUPBOX,
            0, 0, 0, 0);
    }
    for (size_t index = 0; index < kSettings.size(); ++index) {
        gSettingCheckboxes[index] = MakeControl(
            page, L"BUTTON", kSettings[index].title,
            BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 0, 0,
            kFirstSettingId + static_cast<int>(index));
        SendMessageW(gSettingCheckboxes[index], WM_SETFONT,
                     reinterpret_cast<WPARAM>(gBoldFont), TRUE);
        gSettingDescriptions[index] = MakeControl(
            page, L"STATIC", kSettings[index].description, SS_LEFT,
            0, 0, 0, 0);
        gRecommendationLabels[index] = MakeControl(
            page, L"STATIC",
            kSettings[index].recommended ? L"(recommended)" : L"(optional)",
            SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0,
            kFirstRecommendationId + static_cast<int>(index));

        HDC deviceContext = GetDC(page);
        const auto previousFont = SelectObject(deviceContext, gBoldFont);
        SIZE titleSize{};
        GetTextExtentPoint32W(deviceContext, kSettings[index].title,
                              lstrlenW(kSettings[index].title), &titleSize);
        SelectObject(deviceContext, previousFont);
        ReleaseDC(page, deviceContext);
        gSettingTitleWidths[index] = titleSize.cx;
    }
    LayoutSettingsControls();
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            gFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            LOGFONTW boldFontDescription{};
            GetObjectW(gFont, sizeof(boldFontDescription), &boldFontDescription);
            boldFontDescription.lfWeight = FW_BOLD;
            gBoldFont = CreateFontIndirectW(&boldFontDescription);
            if (!gBoldFont) gBoldFont = gFont;
            gTab = MakeControl(window, WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPSIBLINGS,
                               0, 0, 0, 0, kTabId);
            const wchar_t* tabs[] = {L"Settings", L"Crosshair", L"HUD", L"Fonts"};
            for (int i = 0; i < 4; ++i) {
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = const_cast<wchar_t*>(tabs[i]);
                TabCtrl_InsertItem(gTab, i, &item);
            }
            gSettingsPage = MakeControl(window, L"STATIC", L"", WS_VSCROLL,
                                        0, 0, 0, 0, 0);
            gPlaceholderPage = MakeControl(window, L"STATIC", L"", 0, 0, 0, 0, 0);
            MakeControl(gPlaceholderPage, L"STATIC", L"", SS_CENTER, 80, 190, 600, 50, 200);
            CreateSettingsControls(gSettingsPage);
            gStatus = MakeControl(window, L"STATIC", L"", SS_LEFT, 0, 0, 0, 0);
            MakeControl(window, L"BUTTON", L"Defaults", BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, kDefaultsId);
            MakeControl(window, L"BUTTON", L"Reload", BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, kReloadId);
            MakeControl(window, L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, kSaveId);
            BuildIniPath();
            LoadSettings(false);
            ShowSelectedTab();
            return 0;
        }
        case WM_SIZE:
            Layout(window);
            return 0;
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lParam)->idFrom == kTabId &&
                reinterpret_cast<NMHDR*>(lParam)->code == TCN_SELCHANGE) {
                ShowSelectedTab();
                return 0;
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kSaveId: SaveSettings(window); return 0;
                case kReloadId: LoadSettings(false); return 0;
                case kDefaultsId: LoadSettings(true); return 0;
            }
            break;
        case WM_MOUSEWHEEL:
            if (TabCtrl_GetCurSel(gTab) == 0) {
                SendMessageW(gSettingsPage, message, wParam, lParam);
                return 0;
            }
            break;
        case WM_DESTROY:
            if (gBoldFont && gBoldFont != gFont) DeleteObject(gBoldFont);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    INITCOMMONCONTROLSEX commonControls{sizeof(commonControls), ICC_TAB_CLASSES};
    InitCommonControlsEx(&commonControls);

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    windowClass.hIconSm = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (!RegisterClassExW(&windowClass)) return 1;

    HWND window = CreateWindowExW(0, kWindowClass, L"BHD Player Config",
                                  WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 800, 590,
                                  nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}
