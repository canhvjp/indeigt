#pragma once

#include <cstdint>
#include <string>

namespace indeigt {

enum class TimerDecoration : int {
    None = 0,
    Shadow = 1,
    Outline = 2,
};

enum class TimerDisplayAlign : int {
    Auto = 0,
    Left = 1,
    Center = 2,
    Right = 3,
};

struct OverlayConfig {
    // Overlay window geometry (standard movable & resizable window, NOT always-on-top)
    int win_x = 1460;
    int win_y = 32;
    int win_w = 420;
    int win_h = 140;

    // General options
    bool toggle_timer = true;
    bool display_time_only = false; // SpeedRunIGT "simply"
    int decimals = 3;               // 0, 1, 2, 3 (paused/completed always 3)
    bool auto_retime_display = true;

    // IGT options (independent)
    float igt_x = 0.96f;
    float igt_y = 0.12f;
    float igt_scale = 2.0f; // 0.0 hides IGT
    uint32_t igt_color = 0xFFE600;
    TimerDecoration igt_deco = TimerDecoration::Outline;
    TimerDisplayAlign igt_align = TimerDisplayAlign::Right;
    bool igt_use_pause_color = false;
    uint32_t igt_pause_color = 0xFFFFFF;
    bool igt_use_complete_color = true;
    uint32_t igt_complete_color = 0x55FF55;

    // RTA options (independent)
    float rta_x = 0.96f;
    float rta_y = 0.56f;
    float rta_scale = 1.35f; // 0.0 hides RTA
    uint32_t rta_color = 0x9EDBFF;
    TimerDecoration rta_deco = TimerDecoration::Outline;
    TimerDisplayAlign rta_align = TimerDisplayAlign::Right;
    bool rta_use_pause_color = false;
    uint32_t rta_pause_color = 0xFFFFFF;
    bool rta_use_complete_color = true;
    uint32_t rta_complete_color = 0x55FF55;

    // Font & Background options
    std::wstring font_family = L"Consolas";
    bool font_bold = true;
    float bg_opacity = 0.0f;
    int igt_padding = 6;
    int rta_padding = 4;

    // Window background mode: false = 100% transparent see-through, true = solid Color Key for OBS
    bool use_chroma_bg = false;
    uint32_t chroma_bg_color = 0x00AA00;

    // Hide rules (renders blank transparent/key frame so OBS never freezes)
    bool hide_when_no_world = false;
    bool hide_when_not_playing = false;
    bool hide_when_paused = false;
    bool hide_when_mc_closed = false;
    bool only_when_mc_focused = false;
    std::wstring manual_game_dir;

    static std::wstring default_config_path();
    bool save(const std::wstring& path = L"") const;
    static OverlayConfig load(const std::wstring& path = L"");
};

} // namespace indeigt
