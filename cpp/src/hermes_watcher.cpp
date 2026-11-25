#include "hermes_watcher.hpp"
#include "json_mini.hpp"

#include <windows.h>

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace indeigt {

namespace {

std::wstring utf8_to_wide_w(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

bool read_file_shared(const fs::path& p, std::string& out) {
    HANDLE h = CreateFileW(
        p.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(sz.QuadPart));
    DWORD read = 0;
    BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    out.resize(read);
    return true;
}

bool read_file_chunk_shared(const fs::path& p, uintmax_t offset, std::string& out, uintmax_t& new_offset) {
    HANDLE h = CreateFileW(
        p.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz)) {
        CloseHandle(h);
        return false;
    }
    uintmax_t file_size = static_cast<uintmax_t>(sz.QuadPart);
    if (file_size < offset) offset = 0;
    if (file_size == offset) {
        CloseHandle(h);
        return false;
    }

    LARGE_INTEGER seek_pos{};
    seek_pos.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(h, seek_pos, nullptr, FILE_BEGIN)) {
        CloseHandle(h);
        return false;
    }
    uintmax_t to_read = file_size - offset;
    if (to_read > 4 * 1024 * 1024) to_read = 4 * 1024 * 1024;
    out.resize(static_cast<size_t>(to_read));
    DWORD read = 0;
    BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(to_read), &read, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    out.resize(read);
    new_offset = offset + read;
    return true;
}

fs::path resolve_hermes_path(const fs::path& game_dir, const json::Value* obj) {
    if (!obj || !obj->is_object()) return {};
    const auto* p = obj->get("path");
    if (!p || !p->is_string() || p->str_val.empty()) return {};
    std::wstring raw = utf8_to_wide_w(p->str_val);
    bool rel = obj->get("relative") ? obj->get("relative")->as_bool(false) : false;
    return (rel ? (game_dir / raw) : fs::path(raw)).lexically_normal();
}

void collect_title_keys(const json::Value* v, std::string& out) {
    if (!v) return;
    if (v->is_string()) {
        out += v->str_val + " ";
    } else if (v->is_object()) {
        if (const auto* t = v->get("translate"); t && t->is_string()) out += t->str_val + " ";
        if (const auto* t = v->get("text"); t && t->is_string()) out += t->str_val + " ";
        if (const auto* ex = v->get("extra"); ex && ex->is_array()) {
            for (const auto& item : ex->arr_val) collect_title_keys(&item, out);
        }
    } else if (v->is_array()) {
        for (const auto& item : v->arr_val) collect_title_keys(&item, out);
    }
}

bool is_loading_screen(const json::Value* screen) {
    if (!screen || !screen->is_object()) return false;
    const auto* cls_v = screen->get("class");
    if (!cls_v || !cls_v->is_string() || cls_v->str_val.empty()) return false;
    const std::string& cls = cls_v->str_val;

    static const char* kLoadingClasses[] = {
        "class_434", "class_3928", "class_435", "class_412",
        "DownloadingTerrainScreen", "ReceivingLevelScreen",
        "LevelLoadingScreen", "ProgressScreen",
        "GenericDirtMessageScreen", "MessageScreen", "WorkingScreen"
    };
    for (const char* sub : kLoadingClasses) {
        if (cls.find(sub) != std::string::npos) return true;
    }
    std::string title;
    collect_title_keys(screen->get("title"), title);
    static const char* kLoadingTitles[] = {
        "multiplayer.downloadingTerrain", "menu.generatingLevel",
        "menu.loadingLevel", "connect.joining", "menu.savingLevel", "menu.preparingSpawn"
    };
    for (const char* key : kLoadingTitles) {
        if (title.find(key) != std::string::npos) return true;
    }
    return false;
}

bool is_credits_screen(const json::Value* screen) {
    if (!screen || !screen->is_object()) return false;
    const auto* cls_v = screen->get("class");
    if (!cls_v || !cls_v->is_string()) return false;
    const std::string& cls = cls_v->str_val;
    return cls.find("class_445") != std::string::npos ||
           cls.find("CreditsScreen") != std::string::npos ||
           cls.find("WinScreen") != std::string::npos;
}

bool is_wall_or_title_screen(const json::Value* screen) {
    if (!screen || !screen->is_object()) return false;
    const auto* cls_v = screen->get("class");
    if (!cls_v || !cls_v->is_string()) return false;
    const std::string& cls = cls_v->str_val;
    // SeedQueue Wall screen & Title Screen (important for SeedQueue + FastReset)
    return cls.find("SeedQueueWallScreen") != std::string::npos ||
           cls.find("TitleScreen") != std::string::npos ||
           cls.find("class_442") != std::string::npos;
}

int64_t get_file_mtime_ns(const fs::path& p, uintmax_t& out_size) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) {
        return -1;
    }
    ULARGE_INTEGER sz{};
    sz.LowPart = fad.nFileSizeLow;
    sz.HighPart = fad.nFileSizeHigh;
    out_size = static_cast<uintmax_t>(sz.QuadPart);

    ULARGE_INTEGER ft{};
    ft.LowPart = fad.ftLastWriteTime.dwLowDateTime;
    ft.HighPart = fad.ftLastWriteTime.dwHighDateTime;
    return static_cast<int64_t>(ft.QuadPart);
}

} // namespace

HermesSpeedRunWatcher::HermesSpeedRunWatcher(SimulatedInGameTimer& timer) : timer_(timer) {}

void HermesSpeedRunWatcher::set_manual_game_dir(const std::wstring& game_dir) {
    manual_game_dir_ = game_dir;
    force_resync();
}

void HermesSpeedRunWatcher::force_resync() {
    last_discovery_ms_ = 0;
    state_mtime_ = -1;
    state_size_ = 0;
    last_state_raw_.clear();
    record_mtime_ = -1;
    record_size_ = 0;
    events_offset_ = 0;
}

bool HermesSpeedRunWatcher::is_minecraft_focused() const {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD fg_pid = 0;
    GetWindowThreadProcessId(fg, &fg_pid);
    if (fg_pid == 0) return false;
    if (has_active_ && active_.pid == fg_pid) return true;
    for (const auto& inst : all_instances_) {
        if (inst.pid == fg_pid) return true;
    }
    return false;
}

void HermesSpeedRunWatcher::poll(int64_t current_ms) {
    int64_t ts = (current_ms < 0) ? now_ms() : current_ms;

    // Fast foreground PID check: only re-scan EnumWindows if no instance, fg PID changed, or 2.5s elapsed
    HWND fg = GetForegroundWindow();
    DWORD fg_pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &fg_pid);
    bool fg_changed = (fg_pid != 0 && fg_pid != last_fg_pid_ && (!has_active_ || fg_pid != active_.pid));
    if (fg_pid != 0) last_fg_pid_ = fg_pid;

    int64_t interval = has_active_ ? 2500 : 1000;
    if (!has_active_ || fg_changed || (ts - last_discovery_ms_) >= interval) {
        last_discovery_ms_ = ts;
        refresh_instance(ts);
    }

    if (!has_active_) {
        poll_speedrunigt_global_fallback(ts);
        return;
    }

    poll_world_log(ts);
    poll_state_json(ts);

    if (!current_world_path_.empty()) {
        poll_record_json(current_world_path_, ts);
        poll_events_log(current_world_path_, ts);
    }
}

void HermesSpeedRunWatcher::refresh_instance(int64_t ts) {
    if (!manual_game_dir_.empty()) {
        std::error_code ec;
        if (fs::is_directory(manual_game_dir_, ec)) {
            if (!has_active_ || active_.game_dir != manual_game_dir_) {
                active_ = DetectedMinecraft{};
                active_.game_dir = manual_game_dir_;
                has_active_ = true;
                last_state_raw_.clear();
            }
            connected_source_ = L"Manual: " + fs::path(manual_game_dir_).filename().wstring();
            return;
        }
    }

    all_instances_ = detect_minecraft_instances(ts);
    if (all_instances_.empty()) {
        has_active_ = false;
        connected_source_ = L"Waiting for Minecraft...";
        return;
    }

    // 1. Prefer foreground Minecraft instance (exact AATool ActiveInstance behavior)
    const DetectedMinecraft* chosen = nullptr;
    for (const auto& inst : all_instances_) {
        if (inst.is_foreground) {
            chosen = &inst;
            break;
        }
    }

    // 2. Otherwise keep previous active instance if still alive
    if (!chosen && has_active_) {
        for (const auto& inst : all_instances_) {
            if (inst.pid == active_.pid) {
                chosen = &inst;
                break;
            }
        }
    }

    // 3. Otherwise pick instance with most recently updated hermes/state.json
    if (!chosen) {
        int64_t best_mtime = -2;
        for (const auto& inst : all_instances_) {
            uintmax_t sz = 0;
            int64_t mt = get_file_mtime_ns(fs::path(inst.game_dir) / L"hermes" / L"state.json", sz);
            if (mt > best_mtime) {
                best_mtime = mt;
                chosen = &inst;
            }
        }
        if (!chosen) chosen = &all_instances_.front();
    }

    if (!has_active_ || chosen->pid != active_.pid || chosen->game_dir != active_.game_dir) {
        active_ = *chosen;
        has_active_ = true;
        last_state_raw_.clear();
        world_log_path_.clear();
        world_log_offset_ = 0;
    } else {
        active_ = *chosen;
    }

    std::wstring ver = active_.game_version.empty() ? L"" : (L" " + active_.game_version);
    std::wstring via = active_.from_hermes ? L"Hermes+MC" : L"MC Auto-Detect";
    connected_source_ = via + ver + L" (PID " + std::to_wstring(active_.pid) + L")";
}

void HermesSpeedRunWatcher::poll_world_log(int64_t ts) {
    fs::path gdir(active_.game_dir);
    fs::path log_path = active_.world_log_path;
    if (log_path.empty()) {
        std::string latest_name;
        if (read_file_shared(gdir / L"hermes" / L"latest_world_log.txt", latest_name)) {
            while (!latest_name.empty() && (latest_name.back() == '\r' || latest_name.back() == '\n' || latest_name.back() == ' ')) {
                latest_name.pop_back();
            }
            if (!latest_name.empty()) {
                log_path = gdir / L"hermes" / L"world_logs" / utf8_to_wide_w(latest_name);
            }
        }
    }
    if (log_path.empty()) return;

    if (log_path != world_log_path_) {
        world_log_path_ = log_path;
        uintmax_t sz = 0;
        if (get_file_mtime_ns(log_path, sz) >= 0) {
            world_log_offset_ = sz;
        } else {
            world_log_offset_ = 0;
        }
        return;
    }

    std::string chunk;
    uintmax_t new_off = world_log_offset_;
    if (!read_file_chunk_shared(log_path, world_log_offset_, chunk, new_off)) return;
    world_log_offset_ = new_off;

    std::istringstream iss(chunk);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        json::Value entry;
        if (!json::parse(line, entry) || !entry.is_object()) continue;
        std::string etype = entry.get("type") ? entry.get("type")->as_string() : "";
        int64_t etime = entry.get("time") ? entry.get("time")->as_int64(ts) : ts;
        fs::path wpath = resolve_hermes_path(gdir, entry.get("world"));
        if (etype == "entering" && !wpath.empty()) {
            switch_world(wpath, etime);
        } else if (etype == "leave" && !wpath.empty() && wpath == current_world_path_) {
            timer_.on_world_leave(etime);
        }
    }
}

void HermesSpeedRunWatcher::poll_state_json(int64_t ts) {
    fs::path state_path = fs::path(active_.game_dir) / L"hermes" / L"state.json";
    uintmax_t sz = 0;
    int64_t mt = get_file_mtime_ns(state_path, sz);
    if (mt < 0 || (mt == state_mtime_ && sz == state_size_)) {
        return; // File unchanged — skip CreateFile/ReadFile/JSON parse completely!
    }

    std::string raw;
    if (!read_file_shared(state_path, raw) || raw.empty()) {
        return;
    }
    if (raw == last_state_raw_) {
        state_mtime_ = mt;
        state_size_ = sz;
        return;
    }
    json::Value state;
    if (!json::parse(raw, state) || !state.is_object()) {
        return; // Non-atomic write in progress, retry next tick without caching mtime
    }
    state_mtime_ = mt;
    state_size_ = sz;
    last_state_raw_ = std::move(raw);
    apply_hermes_state(state, ts);
}

void HermesSpeedRunWatcher::apply_hermes_state(const json::Value& state, int64_t ts) {
    fs::path gdir(active_.game_dir);
    fs::path world_path = resolve_hermes_path(gdir, state.get("world"));
    const json::Value* screen = state.get("screen");
    bool open_to_lan = state.get("open_to_lan") ? state.get("open_to_lan")->as_bool(false) : false;

    if (world_path.empty() || is_wall_or_title_screen(screen)) {
        if (!current_world_path_.empty() || timer_.status() != TimerStatus::NONE) {
            timer_.on_world_leave(ts);
            timer_.reset_to_none();
            current_world_path_.clear();
            record_mtime_ = -1;
            record_size_ = 0;
            events_path_.clear();
            events_offset_ = 0;
        }
        return;
    }

    if (world_path != current_world_path_) {
        switch_world(world_path, ts);
    }

    if (is_credits_screen(screen)) {
        return; // Wait for record.json completion snap
    }

    bool has_screen_class = screen && screen->is_object() && screen->get("class") && screen->get("class")->is_string() && !screen->get("class")->str_val.empty();
    bool screen_is_pause = (screen && screen->is_object() && screen->get("is_pause")) ? screen->get("is_pause")->as_bool(false) : false;

    if (!has_screen_class) {
        if (timer_.status() != TimerStatus::RUNNING) {
            timer_.set_pause(false, TimerStatus::PAUSED, ts);
        }
    } else if (is_loading_screen(screen)) {
        if (timer_.status() != TimerStatus::IDLE) {
            timer_.set_pause(true, TimerStatus::IDLE, ts);
        }
    } else if (screen_is_pause && !open_to_lan) {
        if (timer_.status() == TimerStatus::RUNNING) {
            timer_.set_pause(true, TimerStatus::PAUSED, ts);
        }
    } else {
        if (timer_.status() == TimerStatus::PAUSED || timer_.status() == TimerStatus::IDLE) {
            timer_.set_pause(false, TimerStatus::PAUSED, ts);
        }
    }
}

void HermesSpeedRunWatcher::switch_world(const fs::path& world_path, int64_t ts) {
    current_world_path_ = world_path;
    record_mtime_ = -1;
    record_size_ = 0;
    events_path_ = world_path / L"speedrunigt" / L"events.log";
    events_offset_ = 0;

    timer_.on_world_enter(world_path.wstring(), world_path.filename().wstring(), ts);

    poll_record_json(world_path, ts);
    uintmax_t sz = 0;
    if (get_file_mtime_ns(events_path_, sz) >= 0) {
        events_offset_ = sz;
    }
}

void HermesSpeedRunWatcher::poll_record_json(const fs::path& world_path, int64_t ts) {
    fs::path rec_path = world_path / L"speedrunigt" / L"record.json";
    uintmax_t sz = 0;
    int64_t mt = get_file_mtime_ns(rec_path, sz);
    if (mt < 0 || (mt == record_mtime_ && sz == record_size_)) return;

    std::string raw;
    if (!read_file_shared(rec_path, raw)) return;
    json::Value doc;
    if (!json::parse(raw, doc) || !doc.is_object()) return;

    record_mtime_ = mt;
    record_size_ = sz;
    timer_.snap_from_record(doc, ts);
}

void HermesSpeedRunWatcher::poll_events_log(const fs::path& world_path, int64_t ts) {
    fs::path ev_path = world_path / L"speedrunigt" / L"events.log";
    std::string chunk;
    uintmax_t new_off = events_offset_;
    if (!read_file_chunk_shared(ev_path, events_offset_, chunk, new_off)) return;
    events_offset_ = new_off;

    std::istringstream iss(chunk);
    std::string line;
    std::string last_id;
    int64_t last_rta = -1, last_igt = -1;

    while (std::getline(iss, line)) {
        std::istringstream ls(line);
        std::string eid;
        int64_t rta = 0, igt = 0;
        if (ls >> eid >> rta >> igt) {
            last_id = std::move(eid);
            last_rta = rta;
            last_igt = igt;
        }
    }
    if (!last_id.empty()) {
        timer_.snap_from_event(last_id, last_rta, last_igt, ts);
    }
}

void HermesSpeedRunWatcher::poll_speedrunigt_global_fallback(int64_t ts) {
    fs::path latest = fs::path(get_speedrunigt_global_folder()) / L"latest_world.json";
    std::string raw;
    if (!read_file_shared(latest, raw)) return;
    json::Value doc;
    if (!json::parse(raw, doc) || !doc.is_object()) return;
    const auto* wp = doc.get("world_path");
    if (!wp || !wp->is_string() || wp->str_val.empty()) return;
    fs::path wpath = utf8_to_wide_w(wp->str_val);
    std::error_code ec;
    if (!fs::is_directory(wpath, ec)) return;

    connected_source_ = L"SpeedRunIGT (latest_world.json)";
    if (wpath != current_world_path_) {
        switch_world(wpath, ts);
    }
    poll_record_json(wpath, ts);
    poll_events_log(wpath, ts);
}

} // namespace indeigt
