#include "overlay_window.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

namespace indeigt {

namespace {

constexpr wchar_t kOverlayClassName[] = L"IndeIGT_TransparentOverlayWindow";
constexpr COLORREF kChromaKeyRef = RGB(1, 0, 1);

Gdiplus::Color rgb_to_gdip(uint32_t rgb, BYTE alpha = 255) {
    BYTE r = static_cast<BYTE>((rgb >> 16) & 0xFF);
    BYTE g = static_cast<BYTE>((rgb >> 8) & 0xFF);
    BYTE b = static_cast<BYTE>(rgb & 0xFF);
    if (r == 1 && g == 0 && b == 1) b = 2;
    return Gdiplus::Color(alpha, r, g, b);
}

void draw_decorated_text(
    Gdiplus::Graphics& g,
    const std::wstring& text,
    const Gdiplus::Font& font,
    float x,
    float y,
    uint32_t rgb,
    TimerDecoration deco,
    float scale) {
    Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsMeasureTrailingSpaces);

    if (deco == TimerDecoration::Outline) {
        float off = std::max(1.0f, std::round(scale * 0.9f));
        Gdiplus::SolidBrush black(Gdiplus::Color(255, 0, 0, 0));
        const float dx[8] = { off, off, off, 0, 0, -off, -off, -off };
        const float dy[8] = { off, 0, -off, -off, off, off, 0, -off };
        for (int i = 0; i < 8; ++i) {
            g.DrawString(text.c_str(), -1, &font, Gdiplus::PointF(x + dx[i], y + dy[i]), &fmt, &black);
        }
    } else if (deco == TimerDecoration::Shadow) {
        float off = std::max(1.0f, std::round(scale * 1.1f));
        Gdiplus::SolidBrush shadow(Gdiplus::Color(255, 42, 42, 42));
        g.DrawString(text.c_str(), -1, &font, Gdiplus::PointF(x + off, y + off), &fmt, &shadow);
    }

    Gdiplus::SolidBrush fg(rgb_to_gdip(rgb, 255));
    g.DrawString(text.c_str(), -1, &font, Gdiplus::PointF(x, y), &fmt, &fg);
}

struct LayoutBox {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

LayoutBox compute_element_layout(
    Gdiplus::Graphics& g,
    const std::wstring& text,
    const Gdiplus::Font& font,
    float norm_x,
    float norm_y,
    TimerDisplayAlign align,
    int win_w,
    int win_h) {
    Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
    Gdiplus::RectF bounds{};
    g.MeasureString(text.c_str(), -1, &font, Gdiplus::PointF(0, 0), &fmt, &bounds);

    float x = norm_x * static_cast<float>(win_w);
    float y = norm_y * static_cast<float>(win_h);
    float w = std::max(1.0f, bounds.Width);
    float h = std::max(1.0f, bounds.Height);

    if (align != TimerDisplayAlign::Left) {
        if (align == TimerDisplayAlign::Right || (align == TimerDisplayAlign::Auto && (x + w > static_cast<float>(win_w)))) {
            x -= w;
        } else if (align == TimerDisplayAlign::Center) {
            x -= w * 0.5f;
        }
    }
    if (y + h > static_cast<float>(win_h)) {
        y -= h;
    }
    x = std::clamp(x, 0.0f, std::max(0.0f, static_cast<float>(win_w) - w));
    y = std::clamp(y, 0.0f, std::max(0.0f, static_cast<float>(win_h) - h));
    return {x, y, w, h};
}

} // namespace

OverlayWindow::OverlayWindow(OverlayConfig& cfg, SimulatedInGameTimer& timer, HermesSpeedRunWatcher& watcher)
    : cfg_(cfg), timer_(timer), watcher_(watcher) {}

OverlayWindow::~OverlayWindow() {
    destroy();
}

std::wstring OverlayWindow::fonts_directory() const {
    wchar_t exe_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    return (fs::path(exe_path).parent_path() / L"fonts").wstring();
}

void OverlayWindow::reload_custom_fonts() {
    private_fonts_ = std::make_unique<Gdiplus::PrivateFontCollection>();
    custom_font_names_.clear();

    fs::path fonts_dir(fonts_directory());
    std::error_code ec;
    fs::create_directories(fonts_dir, ec);
    if (fs::is_directory(fonts_dir, ec)) {
        for (const auto& entry : fs::directory_iterator(fonts_dir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            auto ext = entry.path().extension().wstring();
            for (auto& ch : ext) ch = static_cast<wchar_t>(towlower(ch));
            if (ext == L".ttf" || ext == L".otf") {
                private_fonts_->AddFontFile(entry.path().c_str());
            }
        }
        int count = private_fonts_->GetFamilyCount();
        if (count > 0) {
            std::vector<Gdiplus::FontFamily> families(count);
            int found = 0;
            private_fonts_->GetFamilies(count, families.data(), &found);
            for (int i = 0; i < found; ++i) {
                wchar_t name[LF_FACESIZE] = {};
                if (families[i].GetFamilyName(name) == Gdiplus::Ok) {
                    custom_font_names_.emplace_back(name);
                }
            }
        }
    }
    ++font_generation_;
    force_redraw_ = true;
}

std::wstring OverlayWindow::import_font_file(const std::wstring& src_path) {
    if (src_path.empty()) return {};
    fs::path src(src_path);
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) return {};

    Gdiplus::PrivateFontCollection probe;
    if (probe.AddFontFile(src.c_str()) != Gdiplus::Ok || probe.GetFamilyCount() <= 0) {
        return {};
    }
    Gdiplus::FontFamily fam;
    int found = 0;
    probe.GetFamilies(1, &fam, &found);
    wchar_t fam_name[LF_FACESIZE] = {};
    if (found <= 0 || fam.GetFamilyName(fam_name) != Gdiplus::Ok) {
        return {};
    }

    fs::path dst_dir(fonts_directory());
    fs::create_directories(dst_dir, ec);
    fs::path dst = dst_dir / src.filename();
    if (fs::weakly_canonical(src, ec) != fs::weakly_canonical(dst, ec)) {
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    }

    reload_custom_fonts();
    return std::wstring(fam_name);
}

void OverlayWindow::ensure_cached_fonts() {
    bool dirty = (cached_font_family_ != cfg_.font_family) ||
                 (cached_font_bold_ != cfg_.font_bold) ||
                 (std::fabs(cached_igt_scale_ - cfg_.igt_scale) > 0.001f) ||
                 (std::fabs(cached_rta_scale_ - cfg_.rta_scale) > 0.001f) ||
                 (cached_font_gen_ != font_generation_);
    if (!dirty) return;

    cached_font_family_ = cfg_.font_family;
    cached_font_bold_ = cfg_.font_bold;
    cached_igt_scale_ = cfg_.igt_scale;
    cached_rta_scale_ = cfg_.rta_scale;
    cached_font_gen_ = font_generation_;

    auto build_one = [&](float scale) -> std::unique_ptr<Gdiplus::Font> {
        if (scale <= 0.01f) return nullptr;
        float px_size = 18.0f * scale;
        int style = cfg_.font_bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular;
        if (private_fonts_) {
            int count = private_fonts_->GetFamilyCount();
            if (count > 0) {
                std::vector<Gdiplus::FontFamily> families(count);
                int found = 0;
                private_fonts_->GetFamilies(count, families.data(), &found);
                for (int i = 0; i < found; ++i) {
                    wchar_t name[LF_FACESIZE] = {};
                    if (families[i].GetFamilyName(name) == Gdiplus::Ok && _wcsicmp(name, cfg_.font_family.c_str()) == 0) {
                        if (!families[i].IsStyleAvailable(style)) style = Gdiplus::FontStyleRegular;
                        return std::make_unique<Gdiplus::Font>(&families[i], px_size, style, Gdiplus::UnitPixel);
                    }
                }
            }
        }
        Gdiplus::FontFamily family(cfg_.font_family.c_str());
        if (family.IsAvailable()) {
            if (!family.IsStyleAvailable(style)) style = Gdiplus::FontStyleRegular;
            return std::make_unique<Gdiplus::Font>(&family, px_size, style, Gdiplus::UnitPixel);
        }
        Gdiplus::FontFamily fallback(L"Consolas");
        return std::make_unique<Gdiplus::Font>(&fallback, px_size, style, Gdiplus::UnitPixel);
    };

    cached_igt_font_ = build_one(cfg_.igt_scale);
    cached_rta_font_ = build_one(cfg_.rta_scale);
    force_redraw_ = true;
}

void OverlayWindow::apply_transparency_mode() {
    if (!hwnd_) return;
    if (cfg_.use_chroma_bg) {
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    } else {
        SetLayeredWindowAttributes(hwnd_, kChromaKeyRef, 255, LWA_COLORKEY);
    }
    force_redraw_ = true;
}

bool OverlayWindow::create(HINSTANCE hinst) {
    reload_custom_fonts();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &OverlayWindow::wnd_proc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = CreateSolidBrush(kChromaKeyRef);
    wc.lpszClassName = kOverlayClassName;
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD ex_style = WS_EX_LAYERED;

    RECT rc{0, 0, cfg_.win_w, cfg_.win_h};
    AdjustWindowRectEx(&rc, style, FALSE, ex_style);

    hwnd_ = CreateWindowExW(
        ex_style,
        kOverlayClassName,
        L"IndeIGT Overlay",
        style,
        cfg_.win_x, cfg_.win_y,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hinst, this);
    if (!hwnd_) return false;

    apply_transparency_mode();
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void OverlayWindow::destroy() {
    cached_igt_font_.reset();
    cached_rta_font_.reset();
    release_surface();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void OverlayWindow::ensure_surface(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (mem_dc_ && width_ == width && height_ == height) return;
    release_surface();

    HDC screen_dc = GetDC(nullptr);
    mem_dc_ = CreateCompatibleDC(screen_dc);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    dib_ = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &dib_bits_, nullptr, 0);
    ReleaseDC(nullptr, screen_dc);

    if (dib_) {
        old_bmp_ = static_cast<HBITMAP>(SelectObject(mem_dc_, dib_));
        width_ = width;
        height_ = height;
        force_redraw_ = true;
    }
}

void OverlayWindow::release_surface() {
    if (mem_dc_) {
        if (old_bmp_) SelectObject(mem_dc_, old_bmp_);
        if (dib_) DeleteObject(dib_);
        DeleteDC(mem_dc_);
    }
    mem_dc_ = nullptr;
    dib_ = nullptr;
    old_bmp_ = nullptr;
    dib_bits_ = nullptr;
    width_ = 0;
    height_ = 0;
}

void OverlayWindow::apply_window_style() {
    if (!hwnd_) return;
    apply_transparency_mode();
}

void OverlayWindow::apply_window_geometry() {
    if (!hwnd_) return;
    DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
    DWORD ex_style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
    RECT rc{0, 0, cfg_.win_w, cfg_.win_h};
    AdjustWindowRectEx(&rc, style, FALSE, ex_style);
    SetWindowPos(
        hwnd_,
        nullptr,
        cfg_.win_x, cfg_.win_y,
        rc.right - rc.left, rc.bottom - rc.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
    force_redraw_ = true;
}

bool OverlayWindow::compute_should_hide() const {
    if (!cfg_.toggle_timer) return true;
    if (cfg_.hide_when_mc_closed && !watcher_.has_active_instance()) return true;
    if (cfg_.only_when_mc_focused && !watcher_.is_minecraft_focused()) return true;
    TimerStatus st = timer_.status();
    if (cfg_.hide_when_no_world && (st == TimerStatus::NONE || st == TimerStatus::LEAVE)) return true;
    if (cfg_.hide_when_paused && (st == TimerStatus::PAUSED || st == TimerStatus::IDLE)) return true;
    if (cfg_.hide_when_not_playing && st != TimerStatus::RUNNING && !timer_.is_completed()) return true;
    return false;
}

void OverlayWindow::paint_to_surface(
    const std::wstring& igt_text,
    const std::wstring& rta_text,
    uint32_t igt_col,
    uint32_t rta_col,
    bool hide_timer) {
    if (!mem_dc_ || !dib_bits_) return;

    Gdiplus::Graphics g(mem_dc_);
    if (cfg_.use_chroma_bg) {
        g.Clear(rgb_to_gdip(cfg_.chroma_bg_color, 255));
    } else {
        g.Clear(Gdiplus::Color(255, 1, 0, 1));
    }

    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintSingleBitPerPixelGridFit);

    has_igt_box_ = false;
    has_rta_box_ = false;
    if (hide_timer) return;

    ensure_cached_fonts();

    LayoutBox igt_lay{}, rta_lay{};
    bool draw_igt = (cached_igt_font_ != nullptr);
    bool draw_rta = (cached_rta_font_ != nullptr);

    if (draw_igt) {
        igt_lay = compute_element_layout(g, igt_text, *cached_igt_font_, cfg_.igt_x, cfg_.igt_y, cfg_.igt_align, width_, height_);
        last_igt_box_ = Gdiplus::RectF(igt_lay.x - cfg_.igt_padding, igt_lay.y - cfg_.igt_padding,
                                       igt_lay.w + cfg_.igt_padding * 2.0f, igt_lay.h + cfg_.igt_padding * 2.0f);
        has_igt_box_ = true;
    }

    if (draw_rta) {
        rta_lay = compute_element_layout(g, rta_text, *cached_rta_font_, cfg_.rta_x, cfg_.rta_y, cfg_.rta_align, width_, height_);
        last_rta_box_ = Gdiplus::RectF(rta_lay.x - cfg_.rta_padding, rta_lay.y - cfg_.rta_padding,
                                       rta_lay.w + cfg_.rta_padding * 2.0f, rta_lay.h + cfg_.rta_padding * 2.0f);
        has_rta_box_ = true;
    }

    if (cfg_.bg_opacity > 0.01f) {
        BYTE shade = static_cast<BYTE>(std::clamp(40.0f * (1.0f - cfg_.bg_opacity) + 8.0f, 8.0f, 48.0f));
        Gdiplus::SolidBrush bg_brush(Gdiplus::Color(255, shade, shade, static_cast<BYTE>(shade + 4)));

        if (has_igt_box_ && has_rta_box_ && last_igt_box_.IntersectsWith(last_rta_box_)) {
            Gdiplus::RectF merged{};
            Gdiplus::RectF::Union(merged, last_igt_box_, last_rta_box_);
            g.FillRectangle(&bg_brush, merged);
        } else {
            if (has_rta_box_) g.FillRectangle(&bg_brush, last_rta_box_);
            if (has_igt_box_) g.FillRectangle(&bg_brush, last_igt_box_);
        }
    }

    if (draw_igt) {
        draw_decorated_text(g, igt_text, *cached_igt_font_, igt_lay.x, igt_lay.y, igt_col, cfg_.igt_deco, cfg_.igt_scale);
    }
    if (draw_rta) {
        draw_decorated_text(g, rta_text, *cached_rta_font_, rta_lay.x, rta_lay.y, rta_col, cfg_.rta_deco, cfg_.rta_scale);
    }
}

void OverlayWindow::render(int64_t ts_ms) {
    if (!hwnd_) return;
    last_render_ts_ = ts_ms;

    // When minimized, timer & watcher keep running in main loop; skip unnecessary GDI+ painting until restored
    if (IsIconic(hwnd_)) return;

    RECT cr{};
    GetClientRect(hwnd_, &cr);
    int w = std::max(1, static_cast<int>(cr.right - cr.left));
    int h = std::max(1, static_cast<int>(cr.bottom - cr.top));
    ensure_surface(w, h);

    bool hide_timer = compute_should_hide();
    std::wstring igt_text;
    std::wstring rta_text;
    uint32_t igt_col = cfg_.igt_color;
    uint32_t rta_col = cfg_.rta_color;

    if (!hide_timer) {
        if (cfg_.igt_scale > 0.01f) {
            igt_text = timer_.format_igt(cfg_.decimals, cfg_.display_time_only, cfg_.auto_retime_display, ts_ms);
            if (timer_.is_completed() && cfg_.igt_use_complete_color) igt_col = cfg_.igt_complete_color;
            else if (timer_.is_paused() && !timer_.is_completed() && cfg_.igt_use_pause_color) igt_col = cfg_.igt_pause_color;
        }
        if (cfg_.rta_scale > 0.01f) {
            rta_text = timer_.format_rta(cfg_.decimals, cfg_.display_time_only, ts_ms);
            if (timer_.is_completed() && cfg_.rta_use_complete_color) rta_col = cfg_.rta_complete_color;
            else if (timer_.is_paused() && !timer_.is_completed() && cfg_.rta_use_pause_color) rta_col = cfg_.rta_pause_color;
        }
    }

    // Dirty-frame check: 0% GDI+ CPU when nothing changed!
    if (!force_redraw_ &&
        hide_timer == last_hide_ &&
        w == last_w_ && h == last_h_ &&
        igt_text == last_igt_text_ && rta_text == last_rta_text_ &&
        igt_col == last_igt_col_ && rta_col == last_rta_col_) {
        return;
    }

    force_redraw_ = false;
    last_hide_ = hide_timer;
    last_w_ = w;
    last_h_ = h;
    last_igt_text_ = igt_text;
    last_rta_text_ = rta_text;
    last_igt_col_ = igt_col;
    last_rta_col_ = rta_col;

    paint_to_surface(igt_text, rta_text, igt_col, rta_col, hide_timer);
    if (mem_dc_) {
        HDC hdc = GetDC(hwnd_);
        BitBlt(hdc, 0, 0, width_, height_, mem_dc_, 0, 0, SRCCOPY);
        ReleaseDC(hwnd_, hdc);
    }
}

LRESULT CALLBACK OverlayWindow::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    OverlayWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handle_message(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT OverlayWindow::handle_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            force_redraw_ = true;
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd_, &ps);
            force_redraw_ = true;
            RECT cr{};
            GetClientRect(hwnd_, &cr);
            int w = std::max(1, static_cast<int>(cr.right - cr.left));
            int h = std::max(1, static_cast<int>(cr.bottom - cr.top));
            ensure_surface(w, h);
            int64_t ts = (last_render_ts_ > 0) ? last_render_ts_ : now_ms();
            bool hide_timer = compute_should_hide();
            std::wstring igt_t = (!hide_timer && cfg_.igt_scale > 0.01f) ? timer_.format_igt(cfg_.decimals, cfg_.display_time_only, cfg_.auto_retime_display, ts) : L"";
            std::wstring rta_t = (!hide_timer && cfg_.rta_scale > 0.01f) ? timer_.format_rta(cfg_.decimals, cfg_.display_time_only, ts) : L"";
            uint32_t igt_c = cfg_.igt_color;
            if (timer_.is_completed() && cfg_.igt_use_complete_color) igt_c = cfg_.igt_complete_color;
            else if (timer_.is_paused() && !timer_.is_completed() && cfg_.igt_use_pause_color) igt_c = cfg_.igt_pause_color;
            uint32_t rta_c = cfg_.rta_color;
            if (timer_.is_completed() && cfg_.rta_use_complete_color) rta_c = cfg_.rta_complete_color;
            else if (timer_.is_paused() && !timer_.is_completed() && cfg_.rta_use_pause_color) rta_c = cfg_.rta_pause_color;
            paint_to_surface(igt_t, rta_t, igt_c, rta_c, hide_timer);
            if (mem_dc_) {
                BitBlt(hdc, 0, 0, width_, height_, mem_dc_, 0, 0, SRCCOPY);
            }
            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_KEYDOWN: {
            if (wp == VK_ESCAPE && on_open_settings) {
                on_open_settings();
                return 0;
            }
            return DefWindowProcW(hwnd_, msg, wp, lp);
        }

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = 160;
            mmi->ptMinTrackSize.y = 80;
            return 0;
        }

        case WM_EXITSIZEMOVE: {
            RECT wr{}, cr{};
            GetWindowRect(hwnd_, &wr);
            GetClientRect(hwnd_, &cr);
            cfg_.win_x = wr.left;
            cfg_.win_y = wr.top;
            cfg_.win_w = std::max(120, static_cast<int>(cr.right - cr.left));
            cfg_.win_h = std::max(50, static_cast<int>(cr.bottom - cr.top));
            cfg_.save();
            force_redraw_ = true;
            if (on_config_changed) on_config_changed();
            return 0;
        }

        case WM_LBUTTONDOWN: {
            float mx = static_cast<float>(static_cast<short>(LOWORD(lp)));
            float my = static_cast<float>(static_cast<short>(HIWORD(lp)));
            if (has_igt_box_ && last_igt_box_.Contains(mx, my)) {
                dragging_ = DragTarget::IGT;
                drag_start_x_ = cfg_.igt_x;
                drag_start_y_ = cfg_.igt_y;
            } else if (has_rta_box_ && last_rta_box_.Contains(mx, my)) {
                dragging_ = DragTarget::RTA;
                drag_start_x_ = cfg_.rta_x;
                drag_start_y_ = cfg_.rta_y;
            } else {
                dragging_ = DragTarget::None;
                return 0;
            }
            drag_start_pt_.x = static_cast<short>(LOWORD(lp));
            drag_start_pt_.y = static_cast<short>(HIWORD(lp));
            SetCapture(hwnd_);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (dragging_ != DragTarget::None && width_ > 0 && height_ > 0) {
                int mx = static_cast<short>(LOWORD(lp));
                int my = static_cast<short>(HIWORD(lp));
                float dx = static_cast<float>(mx - drag_start_pt_.x) / static_cast<float>(width_);
                float dy = static_cast<float>(my - drag_start_pt_.y) / static_cast<float>(height_);
                if (dragging_ == DragTarget::IGT) {
                    cfg_.igt_x = std::clamp(drag_start_x_ + dx, 0.0f, 1.0f);
                    cfg_.igt_y = std::clamp(drag_start_y_ + dy, 0.0f, 1.0f);
                } else if (dragging_ == DragTarget::RTA) {
                    cfg_.rta_x = std::clamp(drag_start_x_ + dx, 0.0f, 1.0f);
                    cfg_.rta_y = std::clamp(drag_start_y_ + dy, 0.0f, 1.0f);
                }
                force_redraw_ = true;
                if (on_config_changed) on_config_changed();
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (dragging_ != DragTarget::None) {
                dragging_ = DragTarget::None;
                ReleaseCapture();
                cfg_.save();
                force_redraw_ = true;
                if (on_config_changed) on_config_changed();
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            if (on_open_settings) on_open_settings();
            return 0;
        }

        case WM_CLOSE: {
            if (on_close_requested) on_close_requested();
            else PostQuitMessage(0);
            return 0;
        }

        default:
            return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

} // namespace indeigt
