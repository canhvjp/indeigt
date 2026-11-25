#pragma once

#include "config.hpp"
#include "hermes_watcher.hpp"
#include "overlay_window.hpp"
#include "timer_engine.hpp"

#include <windows.h>
#include <shellapi.h>

#include <functional>

namespace indeigt {

class SettingsWindow {
public:
    SettingsWindow(
        OverlayConfig& cfg,
        SimulatedInGameTimer& timer,
        HermesSpeedRunWatcher& watcher,
        OverlayWindow& overlay);
    ~SettingsWindow();

    bool create(HINSTANCE hinst, bool start_visible = true);
    void destroy();
    void show_and_focus();
    void toggle_visibility();
    void sync_controls_from_config();
    void update_status_text();

    HWND hwnd() const { return hwnd_; }
    std::function<void()> on_quit;

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle_message(UINT msg, WPARAM wp, LPARAM lp);

    void build_ui(HINSTANCE hinst);
    void populate_font_combo();
    void sync_numeric_edits_from_config();
    void read_controls_to_config(bool save_disk);
    void apply_numeric_edit(int control_id);
    void add_tray_icon();
    void remove_tray_icon();
    void show_tray_menu();
    void pick_color(uint32_t& target_rgb);
    void add_font_dialog();
    void browse_game_dir();

    OverlayConfig& cfg_;
    SimulatedInGameTimer& timer_;
    HermesSpeedRunWatcher& watcher_;
    OverlayWindow& overlay_;

    HWND hwnd_ = nullptr;
    HFONT ui_font_ = nullptr;
    NOTIFYICONDATAW nid_{};
    bool tray_added_ = false;
    bool updating_ui_ = false;

    // Live
    HWND lbl_status_ = nullptr;

    // General
    HWND chk_toggle_ = nullptr;
    HWND chk_simply_ = nullptr;
    HWND chk_retime_ = nullptr;
    HWND chk_hide_no_world_ = nullptr;
    HWND chk_hide_not_playing_ = nullptr;
    HWND chk_hide_paused_ = nullptr;
    HWND chk_hide_mc_closed_ = nullptr;
    HWND chk_only_mc_ = nullptr;
    HWND cmb_decimals_ = nullptr;
    HWND edt_win_w_ = nullptr;
    HWND edt_win_h_ = nullptr;

    // IGT (independent pause/complete colors inside IGT section)
    HWND sld_igt_x_ = nullptr;
    HWND edt_igt_x_ = nullptr;
    HWND sld_igt_y_ = nullptr;
    HWND edt_igt_y_ = nullptr;
    HWND sld_igt_scale_ = nullptr;
    HWND edt_igt_scale_ = nullptr;
    HWND cmb_igt_deco_ = nullptr;
    HWND cmb_igt_align_ = nullptr;
    HWND chk_igt_pause_color_ = nullptr;
    HWND chk_igt_complete_color_ = nullptr;

    // RTA (independent pause/complete colors inside RTA section)
    HWND sld_rta_x_ = nullptr;
    HWND edt_rta_x_ = nullptr;
    HWND sld_rta_y_ = nullptr;
    HWND edt_rta_y_ = nullptr;
    HWND sld_rta_scale_ = nullptr;
    HWND edt_rta_scale_ = nullptr;
    HWND cmb_rta_deco_ = nullptr;
    HWND cmb_rta_align_ = nullptr;
    HWND chk_rta_pause_color_ = nullptr;
    HWND chk_rta_complete_color_ = nullptr;

    // Font & Background
    HWND cmb_font_ = nullptr;
    HWND chk_font_bold_ = nullptr;
    HWND chk_chroma_bg_ = nullptr;
    HWND sld_bg_opacity_ = nullptr;
    HWND edt_bg_opacity_ = nullptr;
    HWND sld_igt_pad_ = nullptr;
    HWND edt_igt_pad_ = nullptr;
    HWND sld_rta_pad_ = nullptr;
    HWND edt_rta_pad_ = nullptr;

    // Override
    HWND edt_game_dir_ = nullptr;
};

} // namespace indeigt
