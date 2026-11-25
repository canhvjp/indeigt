#pragma once

#include "config.hpp"
#include "hermes_watcher.hpp"
#include "timer_engine.hpp"

#include <windows.h>
#include <gdiplus.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace indeigt {

class OverlayWindow {
public:
    OverlayWindow(OverlayConfig& cfg, SimulatedInGameTimer& timer, HermesSpeedRunWatcher& watcher);
    ~OverlayWindow();

    bool create(HINSTANCE hinst);
    void destroy();

    HWND hwnd() const { return hwnd_; }
    void apply_window_style();
    void apply_window_geometry();
    void invalidate_render_cache() { force_redraw_ = true; }
    void render(int64_t ts_ms);

    // Font management (.ttf / .otf in fonts\ folder)
    std::wstring fonts_directory() const;
    void reload_custom_fonts();
    std::wstring import_font_file(const std::wstring& src_path);
    const std::vector<std::wstring>& custom_font_names() const { return custom_font_names_; }

    std::function<void()> on_config_changed;
    std::function<void()> on_open_settings;
    std::function<void()> on_close_requested;

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle_message(UINT msg, WPARAM wp, LPARAM lp);

    void apply_transparency_mode();
    void ensure_surface(int width, int height);
    void release_surface();
    void ensure_cached_fonts();
    bool compute_should_hide() const;
    void paint_to_surface(const std::wstring& igt_text, const std::wstring& rta_text, uint32_t igt_col, uint32_t rta_col, bool hide_timer);

    OverlayConfig& cfg_;
    SimulatedInGameTimer& timer_;
    HermesSpeedRunWatcher& watcher_;

    HWND hwnd_ = nullptr;
    HDC mem_dc_ = nullptr;
    HBITMAP dib_ = nullptr;
    HBITMAP old_bmp_ = nullptr;
    void* dib_bits_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int64_t last_render_ts_ = 0;

    // Cached GDI+ fonts (rebuilt only when font/bold/scale changes -> 0 allocs per frame)
    std::unique_ptr<Gdiplus::Font> cached_igt_font_;
    std::unique_ptr<Gdiplus::Font> cached_rta_font_;
    std::wstring cached_font_family_;
    bool cached_font_bold_ = true;
    float cached_igt_scale_ = -1.0f;
    float cached_rta_scale_ = -1.0f;
    uint64_t font_generation_ = 0;
    uint64_t cached_font_gen_ = 0;

    // Dirty-frame cache (skip GDI+ & BitBlt completely when frame is identical)
    bool force_redraw_ = true;
    std::wstring last_igt_text_;
    std::wstring last_rta_text_;
    uint32_t last_igt_col_ = 0;
    uint32_t last_rta_col_ = 0;
    bool last_hide_ = false;
    int last_w_ = 0;
    int last_h_ = 0;

    Gdiplus::RectF last_igt_box_{};
    Gdiplus::RectF last_rta_box_{};
    bool has_igt_box_ = false;
    bool has_rta_box_ = false;

    enum class DragTarget { None, IGT, RTA };
    DragTarget dragging_ = DragTarget::None;
    POINT drag_start_pt_{};
    float drag_start_x_ = 0.0f;
    float drag_start_y_ = 0.0f;

    std::unique_ptr<Gdiplus::PrivateFontCollection> private_fonts_;
    std::vector<std::wstring> custom_font_names_;
};

} // namespace indeigt
