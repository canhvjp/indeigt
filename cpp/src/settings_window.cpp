#include "settings_window.hpp"

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace indeigt {

namespace {

constexpr wchar_t kSettingsClassName[] = L"IndeIGT_SettingsWindow";
constexpr UINT WM_TRAYICON = WM_APP + 1;

enum ControlId : int {
    ID_CHK_TOGGLE = 1001,
    ID_CHK_SIMPLY,
    ID_CHK_RETIME,
    ID_CHK_HIDE_NO_WORLD,
    ID_CHK_HIDE_NOT_PLAYING,
    ID_CHK_HIDE_PAUSED,
    ID_CHK_HIDE_MC_CLOSED,
    ID_CHK_ONLY_MC,
    ID_CMB_DECIMALS,
    ID_EDT_WIN_W,
    ID_EDT_WIN_H,
    ID_BTN_APPLY_SIZE,

    ID_SLD_IGT_X,
    ID_EDT_IGT_X,
    ID_SLD_IGT_Y,
    ID_EDT_IGT_Y,
    ID_SLD_IGT_SCALE,
    ID_EDT_IGT_SCALE,
    ID_BTN_IGT_COLOR,
    ID_CMB_IGT_DECO,
    ID_CMB_IGT_ALIGN,
    ID_CHK_IGT_PAUSE_COLOR,
    ID_BTN_IGT_PAUSE_COLOR,
    ID_CHK_IGT_COMPLETE_COLOR,
    ID_BTN_IGT_COMPLETE_COLOR,

    ID_SLD_RTA_X,
    ID_EDT_RTA_X,
    ID_SLD_RTA_Y,
    ID_EDT_RTA_Y,
    ID_SLD_RTA_SCALE,
    ID_EDT_RTA_SCALE,
    ID_BTN_RTA_COLOR,
    ID_CMB_RTA_DECO,
    ID_CMB_RTA_ALIGN,
    ID_CHK_RTA_PAUSE_COLOR,
    ID_BTN_RTA_PAUSE_COLOR,
    ID_CHK_RTA_COMPLETE_COLOR,
    ID_BTN_RTA_COMPLETE_COLOR,

    ID_CMB_FONT,
    ID_CHK_FONT_BOLD,
    ID_BTN_ADD_FONT,
    ID_BTN_OPEN_FONTS,
    ID_CHK_CHROMA_BG,
    ID_BTN_CHROMA_COLOR,
    ID_SLD_BG_OPACITY,
    ID_EDT_BG_OPACITY,
    ID_SLD_IGT_PAD,
    ID_EDT_IGT_PAD,
    ID_SLD_RTA_PAD,
    ID_EDT_RTA_PAD,

    ID_EDT_GAME_DIR,
    ID_BTN_BROWSE_DIR,
    ID_BTN_RESET_DEF,
    ID_BTN_HIDE_TRAY,
    ID_BTN_QUIT,

    ID_TRAY_SHOW = 2001,
    ID_TRAY_QUIT,
};

HWND make_label(HWND parent, HFONT font, const wchar_t* text, int x, int y, int w, int h) {
    HWND hw = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, x, y, w, h, parent, nullptr, nullptr, nullptr);
    SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return hw;
}

HWND make_group(HWND parent, HFONT font, const wchar_t* text, int x, int y, int w, int h) {
    HWND hw = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, x, y, w, h, parent, nullptr, nullptr, nullptr);
    SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return hw;
}

HWND make_check(HWND parent, HFONT font, const wchar_t* text, int x, int y, int w, int h, int id) {
    HWND hw = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return hw;
}

HWND make_button(HWND parent, HFONT font, const wchar_t* text, int x, int y, int w, int h, int id) {
    HWND hw = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return hw;
}

HWND make_edit(HWND parent, HFONT font, int x, int y, int w, int h, int id, DWORD extra_style = 0) {
    HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | extra_style,
                              x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return hw;
}

HWND make_combo(HWND parent, HFONT font, int x, int y, int w, int h, int id, bool editable = false) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | (editable ? CBS_DROPDOWN : CBS_DROPDOWNLIST);
    HWND hw = CreateWindowExW(0, L"COMBOBOX", L"", style, x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return hw;
}

HWND make_slider(HWND parent, int x, int y, int w, int h, int id, int min_v, int max_v) {
    HWND hw = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(hw, TBM_SETRANGE, TRUE, MAKELPARAM(min_v, max_v));
    return hw;
}

std::wstring format_float(float val, int precision) {
    wchar_t buf[32];
    std::swprintf(buf, 32, precision == 3 ? L"%.3f" : L"%.2f", val);
    return buf;
}

bool parse_edit_float(HWND edit, float& out_val) {
    wchar_t buf[64] = {};
    GetWindowTextW(edit, buf, 64);
    if (!buf[0]) return false;
    for (wchar_t* p = buf; *p; ++p) {
        if (*p == L',') *p = L'.';
    }
    wchar_t* end = nullptr;
    double v = std::wcstod(buf, &end);
    if (end == buf) return false;
    out_val = static_cast<float>(v);
    return true;
}

std::wstring utf8_to_wide_s(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

std::set<std::wstring> enumerate_system_fonts() {
    std::set<std::wstring> fonts;
    HDC hdc = GetDC(nullptr);
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(
        hdc, &lf,
        [](const LOGFONTW* elf, const TEXTMETRICW*, DWORD, LPARAM lp) -> int {
            if (elf && elf->lfFaceName[0] && elf->lfFaceName[0] != L'@') {
                auto* set_ptr = reinterpret_cast<std::set<std::wstring>*>(lp);
                set_ptr->insert(elf->lfFaceName);
            }
            return 1;
        },
        reinterpret_cast<LPARAM>(&fonts),
        0);
    ReleaseDC(nullptr, hdc);
    return fonts;
}

} // namespace

SettingsWindow::SettingsWindow(
    OverlayConfig& cfg,
    SimulatedInGameTimer& timer,
    HermesSpeedRunWatcher& watcher,
    OverlayWindow& overlay)
    : cfg_(cfg), timer_(timer), watcher_(watcher), overlay_(overlay) {}

SettingsWindow::~SettingsWindow() {
    destroy();
}

bool SettingsWindow::create(HINSTANCE hinst, bool start_visible) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsWindow::wnd_proc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kSettingsClassName;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    ui_font_ = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc{0, 0, 620, 770};
    AdjustWindowRectEx(&rc, style, FALSE, 0);

    hwnd_ = CreateWindowExW(
        0,
        kSettingsClassName,
        L"setting",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hinst, this);
    if (!hwnd_) return false;

    build_ui(hinst);
    sync_controls_from_config();
    update_status_text();
    add_tray_icon();

    if (start_visible) {
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
    }
    return true;
}

void SettingsWindow::destroy() {
    remove_tray_icon();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (ui_font_) {
        DeleteObject(ui_font_);
        ui_font_ = nullptr;
    }
}

void SettingsWindow::show_and_focus() {
    if (!hwnd_) return;
    ShowWindow(hwnd_, SW_SHOW);
    ShowWindow(hwnd_, SW_RESTORE);
    SetForegroundWindow(hwnd_);
    sync_controls_from_config();
}

void SettingsWindow::toggle_visibility() {
    if (!hwnd_) return;
    if (IsWindowVisible(hwnd_)) {
        ShowWindow(hwnd_, SW_HIDE);
    } else {
        show_and_focus();
    }
}

void SettingsWindow::populate_font_combo() {
    if (!cmb_font_) return;
    SendMessageW(cmb_font_, CB_RESETCONTENT, 0, 0);

    std::set<std::wstring> added;
    for (const auto& cf : overlay_.custom_font_names()) {
        if (added.insert(cf).second) {
            SendMessageW(cmb_font_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(cf.c_str()));
        }
    }
    for (const auto& sf : enumerate_system_fonts()) {
        if (added.insert(sf).second) {
            SendMessageW(cmb_font_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(sf.c_str()));
        }
    }
    SetWindowTextW(cmb_font_, cfg_.font_family.c_str());
}

void SettingsWindow::build_ui(HINSTANCE) {
    // 1. Live
    make_group(hwnd_, ui_font_, L"Live", 12, 8, 596, 76);
    lbl_status_ = make_label(hwnd_, ui_font_, L"Searching for Minecraft...", 24, 28, 572, 48);

    // 2. General
    make_group(hwnd_, ui_font_, L"General", 12, 90, 596, 134);
    chk_toggle_ = make_check(hwnd_, ui_font_, L"Show Timer", 24, 110, 105, 20, ID_CHK_TOGGLE);
    chk_simply_ = make_check(hwnd_, ui_font_, L"Time Only", 136, 110, 95, 20, ID_CHK_SIMPLY);
    chk_retime_ = make_check(hwnd_, ui_font_, L"Auto-Retime", 238, 110, 110, 20, ID_CHK_RETIME);
    make_label(hwnd_, ui_font_, L"Decimals:", 398, 112, 60, 20);
    cmb_decimals_ = make_combo(hwnd_, ui_font_, 462, 108, 132, 120, ID_CMB_DECIMALS);
    for (const wchar_t* s : {L"0 (NONE)", L"1 (0.0)", L"2 (0.00)", L"3 (0.000)"}) {
        SendMessageW(cmb_decimals_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    }

    chk_hide_no_world_ = make_check(hwnd_, ui_font_, L"Hide outside world (Menu/Wall)", 24, 138, 205, 20, ID_CHK_HIDE_NO_WORLD);
    chk_hide_not_playing_ = make_check(hwnd_, ui_font_, L"Hide when not playing", 236, 138, 160, 20, ID_CHK_HIDE_NOT_PLAYING);
    chk_hide_paused_ = make_check(hwnd_, ui_font_, L"Hide when paused/loading", 404, 138, 190, 20, ID_CHK_HIDE_PAUSED);

    chk_hide_mc_closed_ = make_check(hwnd_, ui_font_, L"Hide when MC closed", 24, 162, 165, 20, ID_CHK_HIDE_MC_CLOSED);
    chk_only_mc_ = make_check(hwnd_, ui_font_, L"Only when MC focused", 236, 162, 165, 20, ID_CHK_ONLY_MC);

    make_label(hwnd_, ui_font_, L"Overlay Size (px):  W:", 24, 192, 130, 20);
    edt_win_w_ = make_edit(hwnd_, ui_font_, 156, 188, 62, 22, ID_EDT_WIN_W, ES_NUMBER);
    make_label(hwnd_, ui_font_, L"H:", 228, 192, 18, 20);
    edt_win_h_ = make_edit(hwnd_, ui_font_, 250, 188, 62, 22, ID_EDT_WIN_H, ES_NUMBER);
    make_button(hwnd_, ui_font_, L"Apply Size", 324, 187, 88, 24, ID_BTN_APPLY_SIZE);

    // 3. IGT (with independent Pause & Complete colors)
    make_group(hwnd_, ui_font_, L"IGT", 12, 232, 292, 242);
    make_label(hwnd_, ui_font_, L"X Pos:", 24, 254, 46, 20);
    sld_igt_x_ = make_slider(hwnd_, 72, 252, 160, 22, ID_SLD_IGT_X, 0, 1000);
    edt_igt_x_ = make_edit(hwnd_, ui_font_, 238, 251, 54, 22, ID_EDT_IGT_X);

    make_label(hwnd_, ui_font_, L"Y Pos:", 24, 282, 46, 20);
    sld_igt_y_ = make_slider(hwnd_, 72, 280, 160, 22, ID_SLD_IGT_Y, 0, 1000);
    edt_igt_y_ = make_edit(hwnd_, ui_font_, 238, 279, 54, 22, ID_EDT_IGT_Y);

    make_label(hwnd_, ui_font_, L"Scale:", 24, 310, 46, 20);
    sld_igt_scale_ = make_slider(hwnd_, 72, 308, 160, 22, ID_SLD_IGT_SCALE, 0, 500);
    edt_igt_scale_ = make_edit(hwnd_, ui_font_, 238, 307, 54, 22, ID_EDT_IGT_SCALE);

    make_label(hwnd_, ui_font_, L"Deco:", 24, 340, 46, 20);
    cmb_igt_deco_ = make_combo(hwnd_, ui_font_, 74, 336, 112, 110, ID_CMB_IGT_DECO);
    for (const wchar_t* s : {L"None", L"Shadow", L"Outline"}) {
        SendMessageW(cmb_igt_deco_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    }
    make_button(hwnd_, ui_font_, L"Color...", 196, 336, 96, 24, ID_BTN_IGT_COLOR);

    make_label(hwnd_, ui_font_, L"Align:", 24, 372, 46, 20);
    cmb_igt_align_ = make_combo(hwnd_, ui_font_, 74, 368, 218, 120, ID_CMB_IGT_ALIGN);
    for (const wchar_t* s : {L"Auto", L"Left", L"Center", L"Right"}) {
        SendMessageW(cmb_igt_align_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    }

    chk_igt_pause_color_ = make_check(hwnd_, ui_font_, L"Pause Color", 24, 404, 150, 20, ID_CHK_IGT_PAUSE_COLOR);
    make_button(hwnd_, ui_font_, L"Color...", 196, 402, 96, 24, ID_BTN_IGT_PAUSE_COLOR);

    chk_igt_complete_color_ = make_check(hwnd_, ui_font_, L"Complete Color", 24, 436, 150, 20, ID_CHK_IGT_COMPLETE_COLOR);
    make_button(hwnd_, ui_font_, L"Color...", 196, 434, 96, 24, ID_BTN_IGT_COMPLETE_COLOR);

    // 4. RTA (with independent Pause & Complete colors)
    make_group(hwnd_, ui_font_, L"RTA", 316, 232, 292, 242);
    make_label(hwnd_, ui_font_, L"X Pos:", 328, 254, 46, 20);
    sld_rta_x_ = make_slider(hwnd_, 376, 252, 160, 22, ID_SLD_RTA_X, 0, 1000);
    edt_rta_x_ = make_edit(hwnd_, ui_font_, 542, 251, 54, 22, ID_EDT_RTA_X);

    make_label(hwnd_, ui_font_, L"Y Pos:", 328, 282, 46, 20);
    sld_rta_y_ = make_slider(hwnd_, 376, 280, 160, 22, ID_SLD_RTA_Y, 0, 1000);
    edt_rta_y_ = make_edit(hwnd_, ui_font_, 542, 279, 54, 22, ID_EDT_RTA_Y);

    make_label(hwnd_, ui_font_, L"Scale:", 328, 310, 46, 20);
    sld_rta_scale_ = make_slider(hwnd_, 376, 308, 160, 22, ID_SLD_RTA_SCALE, 0, 500);
    edt_rta_scale_ = make_edit(hwnd_, ui_font_, 542, 307, 54, 22, ID_EDT_RTA_SCALE);

    make_label(hwnd_, ui_font_, L"Deco:", 328, 340, 46, 20);
    cmb_rta_deco_ = make_combo(hwnd_, ui_font_, 378, 336, 112, 110, ID_CMB_RTA_DECO);
    for (const wchar_t* s : {L"None", L"Shadow", L"Outline"}) {
        SendMessageW(cmb_rta_deco_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    }
    make_button(hwnd_, ui_font_, L"Color...", 500, 336, 96, 24, ID_BTN_RTA_COLOR);

    make_label(hwnd_, ui_font_, L"Align:", 328, 372, 46, 20);
    cmb_rta_align_ = make_combo(hwnd_, ui_font_, 378, 368, 218, 120, ID_CMB_RTA_ALIGN);
    for (const wchar_t* s : {L"Auto", L"Left", L"Center", L"Right"}) {
        SendMessageW(cmb_rta_align_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    }

    chk_rta_pause_color_ = make_check(hwnd_, ui_font_, L"Pause Color", 328, 404, 150, 20, ID_CHK_RTA_PAUSE_COLOR);
    make_button(hwnd_, ui_font_, L"Color...", 500, 402, 96, 24, ID_BTN_RTA_PAUSE_COLOR);

    chk_rta_complete_color_ = make_check(hwnd_, ui_font_, L"Complete Color", 328, 436, 150, 20, ID_CHK_RTA_COMPLETE_COLOR);
    make_button(hwnd_, ui_font_, L"Color...", 500, 434, 96, 24, ID_BTN_RTA_COMPLETE_COLOR);

    // 5. Font
    make_group(hwnd_, ui_font_, L"Font", 12, 480, 596, 154);
    make_label(hwnd_, ui_font_, L"Font:", 24, 502, 38, 20);
    cmb_font_ = make_combo(hwnd_, ui_font_, 66, 498, 240, 240, ID_CMB_FONT, true);
    populate_font_combo();

    chk_font_bold_ = make_check(hwnd_, ui_font_, L"Bold", 316, 500, 54, 20, ID_CHK_FONT_BOLD);
    make_button(hwnd_, ui_font_, L"Add .ttf/.otf...", 378, 497, 108, 24, ID_BTN_ADD_FONT);
    make_button(hwnd_, ui_font_, L"Fonts Folder", 494, 497, 102, 24, ID_BTN_OPEN_FONTS);

    chk_chroma_bg_ = make_check(hwnd_, ui_font_, L"Use Solid Color Key Background (OBS Color Key)", 24, 532, 360, 20, ID_CHK_CHROMA_BG);
    make_button(hwnd_, ui_font_, L"Key Color...", 450, 529, 146, 24, ID_BTN_CHROMA_COLOR);

    make_label(hwnd_, ui_font_, L"Box Opacity:", 24, 564, 76, 20);
    sld_bg_opacity_ = make_slider(hwnd_, 102, 562, 424, 22, ID_SLD_BG_OPACITY, 0, 100);
    edt_bg_opacity_ = make_edit(hwnd_, ui_font_, 534, 561, 44, 22, ID_EDT_BG_OPACITY, ES_NUMBER);
    make_label(hwnd_, ui_font_, L"%", 582, 564, 16, 20);

    make_label(hwnd_, ui_font_, L"IGT Pad:", 24, 596, 56, 20);
    sld_igt_pad_ = make_slider(hwnd_, 84, 594, 152, 22, ID_SLD_IGT_PAD, 0, 40);
    edt_igt_pad_ = make_edit(hwnd_, ui_font_, 242, 593, 44, 22, ID_EDT_IGT_PAD, ES_NUMBER);

    make_label(hwnd_, ui_font_, L"RTA Pad:", 316, 596, 56, 20);
    sld_rta_pad_ = make_slider(hwnd_, 376, 594, 158, 22, ID_SLD_RTA_PAD, 0, 40);
    edt_rta_pad_ = make_edit(hwnd_, ui_font_, 540, 593, 44, 22, ID_EDT_RTA_PAD, ES_NUMBER);

    // 6. Override
    make_group(hwnd_, ui_font_, L"Override", 12, 640, 596, 58);
    edt_game_dir_ = make_edit(hwnd_, ui_font_, 24, 662, 482, 24, ID_EDT_GAME_DIR);
    make_button(hwnd_, ui_font_, L"Browse...", 514, 662, 82, 24, ID_BTN_BROWSE_DIR);

    // Bottom Action Bar
    make_button(hwnd_, ui_font_, L"Reset Defaults", 12, 708, 116, 28, ID_BTN_RESET_DEF);
    make_button(hwnd_, ui_font_, L"Hide to Tray", 396, 708, 104, 28, ID_BTN_HIDE_TRAY);
    make_button(hwnd_, ui_font_, L"Quit", 508, 708, 100, 28, ID_BTN_QUIT);

    make_label(hwnd_, ui_font_, L"Hotkey: Focus Overlay + Esc to open setting", 12, 744, 596, 18);
}

void SettingsWindow::sync_numeric_edits_from_config() {
    SetWindowTextW(edt_igt_x_, format_float(cfg_.igt_x, 3).c_str());
    SetWindowTextW(edt_igt_y_, format_float(cfg_.igt_y, 3).c_str());
    SetWindowTextW(edt_igt_scale_, format_float(cfg_.igt_scale, 2).c_str());

    SetWindowTextW(edt_rta_x_, format_float(cfg_.rta_x, 3).c_str());
    SetWindowTextW(edt_rta_y_, format_float(cfg_.rta_y, 3).c_str());
    SetWindowTextW(edt_rta_scale_, format_float(cfg_.rta_scale, 2).c_str());

    SetWindowTextW(edt_bg_opacity_, std::to_wstring(static_cast<int>(std::lround(cfg_.bg_opacity * 100.0f))).c_str());
    SetWindowTextW(edt_igt_pad_, std::to_wstring(cfg_.igt_padding).c_str());
    SetWindowTextW(edt_rta_pad_, std::to_wstring(cfg_.rta_padding).c_str());
}

void SettingsWindow::sync_controls_from_config() {
    if (!hwnd_) return;
    updating_ui_ = true;

    SendMessageW(chk_toggle_, BM_SETCHECK, cfg_.toggle_timer ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(chk_simply_, BM_SETCHECK, cfg_.display_time_only ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(chk_retime_, BM_SETCHECK, cfg_.auto_retime_display ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(chk_hide_no_world_, BM_SETCHECK, cfg_.hide_when_no_world ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(chk_hide_not_playing_, BM_SETCHECK, cfg_.hide_when_not_playing ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(chk_hide_paused_, BM_SETCHECK, cfg_.hide_when_paused ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(chk_hide_mc_closed_, BM_SETCHECK, cfg_.hide_when_mc_closed ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(chk_only_mc_, BM_SETCHECK, cfg_.only_when_mc_focused ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(chk_chroma_bg_, BM_SETCHECK, cfg_.use_chroma_bg ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(cmb_decimals_, CB_SETCURSEL, std::clamp(cfg_.decimals, 0, 3), 0);

    SetWindowTextW(edt_win_w_, std::to_wstring(cfg_.win_w).c_str());
    SetWindowTextW(edt_win_h_, std::to_wstring(cfg_.win_h).c_str());

    SendMessageW(sld_igt_x_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(cfg_.igt_x * 1000.0f)));
    SendMessageW(sld_igt_y_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(cfg_.igt_y * 1000.0f)));
    SendMessageW(sld_igt_scale_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(cfg_.igt_scale * 100.0f)));
    SendMessageW(cmb_igt_deco_, CB_SETCURSEL, static_cast<int>(cfg_.igt_deco), 0);
    SendMessageW(cmb_igt_align_, CB_SETCURSEL, static_cast<int>(cfg_.igt_align), 0);
    SendMessageW(chk_igt_pause_color_, BM_SETCHECK, cfg_.igt_use_pause_color ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(chk_igt_complete_color_, BM_SETCHECK, cfg_.igt_use_complete_color ? BST_CHECKED : BST_UNCHECKED, 0);

    SendMessageW(sld_rta_x_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(cfg_.rta_x * 1000.0f)));
    SendMessageW(sld_rta_y_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(cfg_.rta_y * 1000.0f)));
    SendMessageW(sld_rta_scale_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(cfg_.rta_scale * 100.0f)));
    SendMessageW(cmb_rta_deco_, CB_SETCURSEL, static_cast<int>(cfg_.rta_deco), 0);
    SendMessageW(cmb_rta_align_, CB_SETCURSEL, static_cast<int>(cfg_.rta_align), 0);
    SendMessageW(chk_rta_pause_color_, BM_SETCHECK, cfg_.rta_use_pause_color ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(chk_rta_complete_color_, BM_SETCHECK, cfg_.rta_use_complete_color ? BST_CHECKED : BST_UNCHECKED, 0);

    SetWindowTextW(cmb_font_, cfg_.font_family.c_str());
    SendMessageW(chk_font_bold_, BM_SETCHECK, cfg_.font_bold ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(sld_bg_opacity_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(cfg_.bg_opacity * 100.0f)));
    SendMessageW(sld_igt_pad_, TBM_SETPOS, TRUE, cfg_.igt_padding);
    SendMessageW(sld_rta_pad_, TBM_SETPOS, TRUE, cfg_.rta_padding);

    sync_numeric_edits_from_config();

    SetWindowTextW(edt_game_dir_, cfg_.manual_game_dir.c_str());
    updating_ui_ = false;
}

void SettingsWindow::read_controls_to_config(bool save_disk) {
    if (updating_ui_ || !hwnd_) return;

    bool old_chroma = cfg_.use_chroma_bg;

    cfg_.toggle_timer = (SendMessageW(chk_toggle_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg_.display_time_only = (SendMessageW(chk_simply_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg_.auto_retime_display = (SendMessageW(chk_retime_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg_.hide_when_no_world = (SendMessageW(chk_hide_no_world_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg_.hide_when_not_playing = (SendMessageW(chk_hide_not_playing_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg_.hide_when_paused = (SendMessageW(chk_hide_paused_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg_.hide_when_mc_closed = (SendMessageW(chk_hide_mc_closed_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg_.only_when_mc_focused = (SendMessageW(chk_only_mc_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg_.use_chroma_bg = (SendMessageW(chk_chroma_bg_, BM_GETCHECK, 0, 0) == BST_CHECKED);

    int dec = static_cast<int>(SendMessageW(cmb_decimals_, CB_GETCURSEL, 0, 0));
    if (dec >= 0) cfg_.decimals = std::clamp(dec, 0, 3);

    cfg_.igt_x = static_cast<float>(SendMessageW(sld_igt_x_, TBM_GETPOS, 0, 0)) / 1000.0f;
    cfg_.igt_y = static_cast<float>(SendMessageW(sld_igt_y_, TBM_GETPOS, 0, 0)) / 1000.0f;
    cfg_.igt_scale = static_cast<float>(SendMessageW(sld_igt_scale_, TBM_GETPOS, 0, 0)) / 100.0f;
    int deco = static_cast<int>(SendMessageW(cmb_igt_deco_, CB_GETCURSEL, 0, 0));
    if (deco >= 0) cfg_.igt_deco = static_cast<TimerDecoration>(deco);
    int align = static_cast<int>(SendMessageW(cmb_igt_align_, CB_GETCURSEL, 0, 0));
    if (align >= 0) cfg_.igt_align = static_cast<TimerDisplayAlign>(align);
    cfg_.igt_use_pause_color = (SendMessageW(chk_igt_pause_color_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg_.igt_use_complete_color = (SendMessageW(chk_igt_complete_color_, BM_GETCHECK, 0, 0) == BST_CHECKED);

    cfg_.rta_x = static_cast<float>(SendMessageW(sld_rta_x_, TBM_GETPOS, 0, 0)) / 1000.0f;
    cfg_.rta_y = static_cast<float>(SendMessageW(sld_rta_y_, TBM_GETPOS, 0, 0)) / 1000.0f;
    cfg_.rta_scale = static_cast<float>(SendMessageW(sld_rta_scale_, TBM_GETPOS, 0, 0)) / 100.0f;
    deco = static_cast<int>(SendMessageW(cmb_rta_deco_, CB_GETCURSEL, 0, 0));
    if (deco >= 0) cfg_.rta_deco = static_cast<TimerDecoration>(deco);
    align = static_cast<int>(SendMessageW(cmb_rta_align_, CB_GETCURSEL, 0, 0));
    if (align >= 0) cfg_.rta_align = static_cast<TimerDisplayAlign>(align);
    cfg_.rta_use_pause_color = (SendMessageW(chk_rta_pause_color_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg_.rta_use_complete_color = (SendMessageW(chk_rta_complete_color_, BM_GETCHECK, 0, 0) == BST_CHECKED);

    wchar_t font_buf[128] = {};
    GetWindowTextW(cmb_font_, font_buf, 128);
    if (font_buf[0]) cfg_.font_family = font_buf;
    cfg_.font_bold = (SendMessageW(chk_font_bold_, BM_GETCHECK, 0, 0) == BST_CHECKED);

    cfg_.bg_opacity = static_cast<float>(SendMessageW(sld_bg_opacity_, TBM_GETPOS, 0, 0)) / 100.0f;
    cfg_.igt_padding = static_cast<int>(SendMessageW(sld_igt_pad_, TBM_GETPOS, 0, 0));
    cfg_.rta_padding = static_cast<int>(SendMessageW(sld_rta_pad_, TBM_GETPOS, 0, 0));

    wchar_t dir_buf[MAX_PATH] = {};
    GetWindowTextW(edt_game_dir_, dir_buf, MAX_PATH);
    if (cfg_.manual_game_dir != dir_buf) {
        cfg_.manual_game_dir = dir_buf;
        watcher_.set_manual_game_dir(cfg_.manual_game_dir);
    }

    updating_ui_ = true;
    sync_numeric_edits_from_config();
    updating_ui_ = false;

    if (old_chroma != cfg_.use_chroma_bg) {
        overlay_.apply_window_style();
    } else {
        overlay_.invalidate_render_cache();
    }
    if (save_disk) cfg_.save();
}

void SettingsWindow::apply_numeric_edit(int control_id) {
    if (updating_ui_ || !hwnd_) return;
    float v = 0.0f;
    bool changed = false;

    updating_ui_ = true;
    switch (control_id) {
        case ID_EDT_IGT_X:
            if (parse_edit_float(edt_igt_x_, v)) {
                cfg_.igt_x = std::clamp(v, 0.0f, 1.0f);
                SendMessageW(sld_igt_x_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(cfg_.igt_x * 1000.0f)));
                changed = true;
            }
            break;
        case ID_EDT_IGT_Y:
            if (parse_edit_float(edt_igt_y_, v)) {
                cfg_.igt_y = std::clamp(v, 0.0f, 1.0f);
                SendMessageW(sld_igt_y_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(cfg_.igt_y * 1000.0f)));
                changed = true;
            }
            break;
        case ID_EDT_IGT_SCALE:
            if (parse_edit_float(edt_igt_scale_, v)) {
                cfg_.igt_scale = std::clamp(v, 0.0f, 5.0f);
                SendMessageW(sld_igt_scale_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(cfg_.igt_scale * 100.0f)));
                changed = true;
            }
            break;
        case ID_EDT_RTA_X:
            if (parse_edit_float(edt_rta_x_, v)) {
                cfg_.rta_x = std::clamp(v, 0.0f, 1.0f);
                SendMessageW(sld_rta_x_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(cfg_.rta_x * 1000.0f)));
                changed = true;
            }
            break;
        case ID_EDT_RTA_Y:
            if (parse_edit_float(edt_rta_y_, v)) {
                cfg_.rta_y = std::clamp(v, 0.0f, 1.0f);
                SendMessageW(sld_rta_y_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(cfg_.rta_y * 1000.0f)));
                changed = true;
            }
            break;
        case ID_EDT_RTA_SCALE:
            if (parse_edit_float(edt_rta_scale_, v)) {
                cfg_.rta_scale = std::clamp(v, 0.0f, 5.0f);
                SendMessageW(sld_rta_scale_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(cfg_.rta_scale * 100.0f)));
                changed = true;
            }
            break;
        case ID_EDT_BG_OPACITY:
            if (parse_edit_float(edt_bg_opacity_, v)) {
                int pct = std::clamp(static_cast<int>(std::lround(v)), 0, 100);
                cfg_.bg_opacity = static_cast<float>(pct) / 100.0f;
                SendMessageW(sld_bg_opacity_, TBM_SETPOS, TRUE, pct);
                changed = true;
            }
            break;
        case ID_EDT_IGT_PAD:
            if (parse_edit_float(edt_igt_pad_, v)) {
                cfg_.igt_padding = std::clamp(static_cast<int>(std::lround(v)), 0, 40);
                SendMessageW(sld_igt_pad_, TBM_SETPOS, TRUE, cfg_.igt_padding);
                changed = true;
            }
            break;
        case ID_EDT_RTA_PAD:
            if (parse_edit_float(edt_rta_pad_, v)) {
                cfg_.rta_padding = std::clamp(static_cast<int>(std::lround(v)), 0, 40);
                SendMessageW(sld_rta_pad_, TBM_SETPOS, TRUE, cfg_.rta_padding);
                changed = true;
            }
            break;
        default:
            break;
    }
    updating_ui_ = false;
    if (changed) {
        overlay_.invalidate_render_cache();
        cfg_.save();
    }
}

void SettingsWindow::update_status_text() {
    if (!lbl_status_ || !IsWindowVisible(hwnd_)) return;
    std::wstring world = timer_.world_name().empty() ? L"(none)" : timer_.world_name();
    std::wstring gdir = watcher_.has_active_instance() ? watcher_.active_instance().game_dir : L"(auto-detecting)";
    if (gdir.size() > 70) gdir = L"..." + gdir.substr(gdir.size() - 67);

    const auto& snap = timer_.last_snap();
    wchar_t buf[512];
    std::swprintf(
        buf, 512,
        L"Instance: %ls  |  Status: %ls\nGameDir: %ls\nWorld: %ls  |  Last Snap: %ls (IGT %+lldms, RTA %+lldms)",
        watcher_.connected_source().c_str(),
        status_name(timer_.status()),
        gdir.c_str(),
        world.c_str(),
        utf8_to_wide_s(snap.source).c_str(),
        snap.igt_drift_ms,
        snap.rta_drift_ms);
    SetWindowTextW(lbl_status_, buf);
}

void SettingsWindow::add_tray_icon() {
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid_.szTip, L"IndeIGT Overlay");
    tray_added_ = (Shell_NotifyIconW(NIM_ADD, &nid_) == TRUE);
}

void SettingsWindow::remove_tray_icon() {
    if (tray_added_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        tray_added_ = false;
    }
}

void SettingsWindow::show_tray_menu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"Open setting");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_QUIT, L"Quit");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}

void SettingsWindow::pick_color(uint32_t& target_rgb) {
    static COLORREF custom_colors[16] = {
        RGB(255, 230, 0), RGB(158, 219, 255), RGB(255, 255, 255), RGB(85, 255, 85),
        RGB(255, 85, 85), RGB(255, 170, 0), RGB(170, 0, 255), RGB(0, 170, 0)
    };
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd_;
    cc.lpCustColors = custom_colors;
    cc.rgbResult = RGB((target_rgb >> 16) & 0xFF, (target_rgb >> 8) & 0xFF, target_rgb & 0xFF);
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc)) {
        target_rgb = (static_cast<uint32_t>(GetRValue(cc.rgbResult)) << 16) |
                     (static_cast<uint32_t>(GetGValue(cc.rgbResult)) << 8) |
                     static_cast<uint32_t>(GetBValue(cc.rgbResult));
        overlay_.invalidate_render_cache();
        cfg_.save();
    }
}

void SettingsWindow::add_font_dialog() {
    wchar_t file_buf[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"Font Files (*.ttf;*.otf)\0*.ttf;*.otf\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Select a .ttf or .otf Font File";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        std::wstring loaded_name = overlay_.import_font_file(file_buf);
        if (!loaded_name.empty()) {
            cfg_.font_family = loaded_name;
            cfg_.save();
            populate_font_combo();
            overlay_.invalidate_render_cache();
        } else {
            MessageBoxW(hwnd_, L"Could not load the selected font file. Please choose a valid .ttf or .otf font.", L"IndeIGT", MB_ICONWARNING);
        }
    }
}

void SettingsWindow::browse_game_dir() {
    BROWSEINFOW bi{};
    bi.hwndOwner = hwnd_;
    bi.lpszTitle = L"Select Minecraft Game Directory (.minecraft)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH] = {};
        if (SHGetPathFromIDListW(pidl, path)) {
            SetWindowTextW(edt_game_dir_, path);
            read_controls_to_config(true);
        }
        CoTaskMemFree(pidl);
    }
}

LRESULT CALLBACK SettingsWindow::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handle_message(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT SettingsWindow::handle_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CLOSE:
            ShowWindow(hwnd_, SW_HIDE);
            return 0;

        case WM_HSCROLL:
            read_controls_to_config(LOWORD(wp) == TB_ENDTRACK);
            return 0;

        case WM_COMMAND: {
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            switch (id) {
                case ID_EDT_IGT_X:
                case ID_EDT_IGT_Y:
                case ID_EDT_IGT_SCALE:
                case ID_EDT_RTA_X:
                case ID_EDT_RTA_Y:
                case ID_EDT_RTA_SCALE:
                case ID_EDT_BG_OPACITY:
                case ID_EDT_IGT_PAD:
                case ID_EDT_RTA_PAD:
                    if (code == EN_CHANGE) {
                        apply_numeric_edit(id);
                    } else if (code == EN_KILLFOCUS) {
                        updating_ui_ = true;
                        sync_numeric_edits_from_config();
                        updating_ui_ = false;
                    }
                    return 0;

                case ID_BTN_IGT_COLOR:
                    pick_color(cfg_.igt_color);
                    return 0;
                case ID_BTN_IGT_PAUSE_COLOR:
                    pick_color(cfg_.igt_pause_color);
                    return 0;
                case ID_BTN_IGT_COMPLETE_COLOR:
                    pick_color(cfg_.igt_complete_color);
                    return 0;
                case ID_BTN_RTA_COLOR:
                    pick_color(cfg_.rta_color);
                    return 0;
                case ID_BTN_RTA_PAUSE_COLOR:
                    pick_color(cfg_.rta_pause_color);
                    return 0;
                case ID_BTN_RTA_COMPLETE_COLOR:
                    pick_color(cfg_.rta_complete_color);
                    return 0;
                case ID_BTN_CHROMA_COLOR:
                    pick_color(cfg_.chroma_bg_color);
                    return 0;
                case ID_BTN_ADD_FONT:
                    add_font_dialog();
                    return 0;
                case ID_BTN_OPEN_FONTS: {
                    std::wstring fdir = overlay_.fonts_directory();
                    ShellExecuteW(hwnd_, L"open", fdir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
                case ID_BTN_APPLY_SIZE: {
                    wchar_t wb[32] = {}, hb[32] = {};
                    GetWindowTextW(edt_win_w_, wb, 32);
                    GetWindowTextW(edt_win_h_, hb, 32);
                    int nw = std::clamp(_wtoi(wb), 120, 3840);
                    int nh = std::clamp(_wtoi(hb), 50, 2160);
                    cfg_.win_w = nw;
                    cfg_.win_h = nh;
                    cfg_.save();
                    overlay_.apply_window_geometry();
                    sync_controls_from_config();
                    return 0;
                }
                case ID_BTN_BROWSE_DIR:
                    browse_game_dir();
                    return 0;
                case ID_BTN_RESET_DEF: {
                    cfg_ = OverlayConfig{};
                    cfg_.save();
                    sync_controls_from_config();
                    overlay_.apply_window_style();
                    overlay_.apply_window_geometry();
                    return 0;
                }
                case ID_BTN_HIDE_TRAY:
                    ShowWindow(hwnd_, SW_HIDE);
                    return 0;
                case ID_BTN_QUIT:
                case ID_TRAY_QUIT:
                    if (on_quit) on_quit();
                    return 0;
                case ID_TRAY_SHOW:
                    show_and_focus();
                    return 0;
                case ID_CMB_FONT:
                    if (code == CBN_SELCHANGE) {
                        int idx = static_cast<int>(SendMessageW(cmb_font_, CB_GETCURSEL, 0, 0));
                        if (idx >= 0) {
                            wchar_t buf[128] = {};
                            SendMessageW(cmb_font_, CB_GETLBTEXT, idx, reinterpret_cast<LPARAM>(buf));
                            cfg_.font_family = buf;
                            overlay_.invalidate_render_cache();
                            cfg_.save();
                        }
                    } else if (code == CBN_EDITCHANGE) {
                        read_controls_to_config(true);
                    }
                    return 0;
                case ID_EDT_GAME_DIR:
                    if (code == EN_CHANGE) read_controls_to_config(true);
                    return 0;
                case ID_EDT_WIN_W:
                case ID_EDT_WIN_H:
                    return 0;
                default:
                    if (code == BN_CLICKED || code == CBN_SELCHANGE) {
                        read_controls_to_config(true);
                    }
                    return 0;
            }
        }

        case WM_TRAYICON:
            if (lp == WM_LBUTTONDBLCLK) {
                show_and_focus();
            } else if (lp == WM_RBUTTONUP) {
                show_tray_menu();
            }
            return 0;

        default:
            return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

} // namespace indeigt
