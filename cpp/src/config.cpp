#include "config.hpp"
#include "json_mini.hpp"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace indeigt {

static std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), len, nullptr, nullptr);
    return s;
}

static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

std::wstring OverlayConfig::default_config_path() {
    wchar_t exe_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::wstring p(exe_path);
    size_t slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        p = p.substr(0, slash + 1);
    }
    return p + L"config.json";
}

bool OverlayConfig::save(const std::wstring& path) const {
    std::wstring target = path.empty() ? default_config_path() : path;
    json::Value root = json::Value::make_object();

    root["win_x"] = json::Value::make_int(win_x);
    root["win_y"] = json::Value::make_int(win_y);
    root["win_w"] = json::Value::make_int(win_w);
    root["win_h"] = json::Value::make_int(win_h);

    root["toggle_timer"] = json::Value::make_bool(toggle_timer);
    root["display_time_only"] = json::Value::make_bool(display_time_only);
    root["decimals"] = json::Value::make_int(decimals);
    root["auto_retime_display"] = json::Value::make_bool(auto_retime_display);

    root["igt_x"] = json::Value::make_number(igt_x);
    root["igt_y"] = json::Value::make_number(igt_y);
    root["igt_scale"] = json::Value::make_number(igt_scale);
    root["igt_color"] = json::Value::make_int(static_cast<int64_t>(igt_color & 0xFFFFFF));
    root["igt_deco"] = json::Value::make_int(static_cast<int>(igt_deco));
    root["igt_align"] = json::Value::make_int(static_cast<int>(igt_align));
    root["igt_use_pause_color"] = json::Value::make_bool(igt_use_pause_color);
    root["igt_pause_color"] = json::Value::make_int(static_cast<int64_t>(igt_pause_color & 0xFFFFFF));
    root["igt_use_complete_color"] = json::Value::make_bool(igt_use_complete_color);
    root["igt_complete_color"] = json::Value::make_int(static_cast<int64_t>(igt_complete_color & 0xFFFFFF));

    root["rta_x"] = json::Value::make_number(rta_x);
    root["rta_y"] = json::Value::make_number(rta_y);
    root["rta_scale"] = json::Value::make_number(rta_scale);
    root["rta_color"] = json::Value::make_int(static_cast<int64_t>(rta_color & 0xFFFFFF));
    root["rta_deco"] = json::Value::make_int(static_cast<int>(rta_deco));
    root["rta_align"] = json::Value::make_int(static_cast<int>(rta_align));
    root["rta_use_pause_color"] = json::Value::make_bool(rta_use_pause_color);
    root["rta_pause_color"] = json::Value::make_int(static_cast<int64_t>(rta_pause_color & 0xFFFFFF));
    root["rta_use_complete_color"] = json::Value::make_bool(rta_use_complete_color);
    root["rta_complete_color"] = json::Value::make_int(static_cast<int64_t>(rta_complete_color & 0xFFFFFF));

    root["font_family"] = json::Value::make_string(wide_to_utf8(font_family));
    root["font_bold"] = json::Value::make_bool(font_bold);
    root["bg_opacity"] = json::Value::make_number(bg_opacity);
    root["igt_padding"] = json::Value::make_int(igt_padding);
    root["rta_padding"] = json::Value::make_int(rta_padding);

    root["use_chroma_bg"] = json::Value::make_bool(use_chroma_bg);
    root["chroma_bg_color"] = json::Value::make_int(static_cast<int64_t>(chroma_bg_color & 0xFFFFFF));

    root["hide_when_no_world"] = json::Value::make_bool(hide_when_no_world);
    root["hide_when_not_playing"] = json::Value::make_bool(hide_when_not_playing);
    root["hide_when_paused"] = json::Value::make_bool(hide_when_paused);
    root["hide_when_mc_closed"] = json::Value::make_bool(hide_when_mc_closed);
    root["only_when_mc_focused"] = json::Value::make_bool(only_when_mc_focused);
    root["manual_game_dir"] = json::Value::make_string(wide_to_utf8(manual_game_dir));

    std::ofstream ofs(target, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    std::string dumped = json::dump(root, 2);
    ofs.write(dumped.data(), static_cast<std::streamsize>(dumped.size()));
    return static_cast<bool>(ofs);
}

OverlayConfig OverlayConfig::load(const std::wstring& path) {
    OverlayConfig cfg;
    std::wstring target = path.empty() ? default_config_path() : path;
    std::ifstream ifs(target, std::ios::binary);
    if (!ifs) {
        cfg.save(target);
        return cfg;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    json::Value root;
    if (!json::parse(oss.str(), root) || !root.is_object()) {
        return cfg;
    }

    auto get_bool = [&](const char* k, bool& dst) {
        if (const auto* v = root.get(k)) dst = v->as_bool(dst);
    };
    auto get_int = [&](const char* k, int& dst) {
        if (const auto* v = root.get(k)) dst = v->as_int(dst);
    };
    auto get_float = [&](const char* k, float& dst) {
        if (const auto* v = root.get(k)) dst = v->as_float(dst);
    };
    auto get_rgb = [&](const char* k, uint32_t& dst) {
        if (const auto* v = root.get(k)) dst = static_cast<uint32_t>(v->as_int64(dst)) & 0xFFFFFF;
    };

    get_int("win_x", cfg.win_x);
    get_int("win_y", cfg.win_y);
    get_int("win_w", cfg.win_w);
    get_int("win_h", cfg.win_h);
    cfg.win_w = std::clamp(cfg.win_w, 120, 3840);
    cfg.win_h = std::clamp(cfg.win_h, 50, 2160);

    get_bool("toggle_timer", cfg.toggle_timer);
    get_bool("display_time_only", cfg.display_time_only);
    get_int("decimals", cfg.decimals);
    cfg.decimals = std::clamp(cfg.decimals, 0, 3);
    get_bool("auto_retime_display", cfg.auto_retime_display);

    get_float("igt_x", cfg.igt_x);
    get_float("igt_y", cfg.igt_y);
    get_float("igt_scale", cfg.igt_scale);
    get_rgb("igt_color", cfg.igt_color);
    int deco = 2, align = 3;
    get_int("igt_deco", deco);
    cfg.igt_deco = static_cast<TimerDecoration>(std::clamp(deco, 0, 2));
    get_int("igt_align", align);
    cfg.igt_align = static_cast<TimerDisplayAlign>(std::clamp(align, 0, 3));
    get_bool("igt_use_pause_color", cfg.igt_use_pause_color);
    get_rgb("igt_pause_color", cfg.igt_pause_color);
    get_bool("igt_use_complete_color", cfg.igt_use_complete_color);
    get_rgb("igt_complete_color", cfg.igt_complete_color);

    get_float("rta_x", cfg.rta_x);
    get_float("rta_y", cfg.rta_y);
    get_float("rta_scale", cfg.rta_scale);
    get_rgb("rta_color", cfg.rta_color);
    deco = 2; align = 3;
    get_int("rta_deco", deco);
    cfg.rta_deco = static_cast<TimerDecoration>(std::clamp(deco, 0, 2));
    get_int("rta_align", align);
    cfg.rta_align = static_cast<TimerDisplayAlign>(std::clamp(align, 0, 3));
    get_bool("rta_use_pause_color", cfg.rta_use_pause_color);
    get_rgb("rta_pause_color", cfg.rta_pause_color);
    get_bool("rta_use_complete_color", cfg.rta_use_complete_color);
    get_rgb("rta_complete_color", cfg.rta_complete_color);

    if (const auto* v = root.get("font_family")) {
        std::wstring f = utf8_to_wide(v->as_string());
        if (!f.empty()) cfg.font_family = f;
    }
    get_bool("font_bold", cfg.font_bold);
    get_float("bg_opacity", cfg.bg_opacity);
    cfg.bg_opacity = std::clamp(cfg.bg_opacity, 0.0f, 1.0f);
    get_int("igt_padding", cfg.igt_padding);
    cfg.igt_padding = std::clamp(cfg.igt_padding, 0, 40);
    get_int("rta_padding", cfg.rta_padding);
    cfg.rta_padding = std::clamp(cfg.rta_padding, 0, 40);

    get_bool("use_chroma_bg", cfg.use_chroma_bg);
    get_rgb("chroma_bg_color", cfg.chroma_bg_color);

    get_bool("hide_when_no_world", cfg.hide_when_no_world);
    get_bool("hide_when_not_playing", cfg.hide_when_not_playing);
    get_bool("hide_when_paused", cfg.hide_when_paused);
    get_bool("hide_when_mc_closed", cfg.hide_when_mc_closed);
    get_bool("only_when_mc_focused", cfg.only_when_mc_focused);
    if (const auto* v = root.get("manual_game_dir")) {
        cfg.manual_game_dir = utf8_to_wide(v->as_string());
    }
    return cfg;
}

} // namespace indeigt
