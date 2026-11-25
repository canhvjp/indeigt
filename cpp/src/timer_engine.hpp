#pragma once

#include "json_mini.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace indeigt {

enum class TimerStatus {
    NONE = -1,
    RUNNING = 0,
    PAUSED = 1,
    COMPLETED_LEGACY = 101,
    LEAVE = 102,
    IDLE = 2,
};

inline int pause_priority(TimerStatus st) {
    switch (st) {
        case TimerStatus::NONE: return -1;
        case TimerStatus::RUNNING: return 0;
        case TimerStatus::PAUSED:
        case TimerStatus::COMPLETED_LEGACY:
        case TimerStatus::LEAVE: return 1;
        case TimerStatus::IDLE: return 2;
    }
    return 0;
}

inline const wchar_t* status_name(TimerStatus st) {
    switch (st) {
        case TimerStatus::NONE: return L"NONE";
        case TimerStatus::RUNNING: return L"RUNNING";
        case TimerStatus::PAUSED: return L"PAUSED";
        case TimerStatus::COMPLETED_LEGACY: return L"COMPLETED";
        case TimerStatus::LEAVE: return L"LEAVE";
        case TimerStatus::IDLE: return L"IDLE";
    }
    return L"NONE";
}

int64_t now_ms();
std::wstring time_to_string_format(int64_t time_ms);
std::wstring get_time_format(int64_t time_ms, int decimals, bool is_paused_or_completed);

struct SnapInfo {
    std::string source = "none";
    int64_t timestamp_ms = 0;
    int64_t igt_drift_ms = 0;
    int64_t rta_drift_ms = 0;
};

class SimulatedInGameTimer {
public:
    SimulatedInGameTimer();

    void reset_to_none();

    // State predicates (exact mirrors of SpeedRunIGT InGameTimer.java)
    bool is_started() const { return start_time_ != 0; }
    bool is_stopped() const { return status_ == TimerStatus::LEAVE || status_ == TimerStatus::NONE; }
    bool is_paused() const { return status_ != TimerStatus::COMPLETED_LEGACY && pause_priority(status_) > 0; }
    bool is_playing() const { return !is_paused() && status_ != TimerStatus::COMPLETED_LEGACY && status_ != TimerStatus::NONE; }
    bool is_completed() const { return is_completed_ || status_ == TimerStatus::COMPLETED_LEGACY; }
    TimerStatus status() const { return status_; }

    const std::wstring& world_path() const { return world_path_; }
    const std::wstring& world_name() const { return world_name_; }
    const std::string& category() const { return category_; }
    const SnapInfo& last_snap() const { return last_snap_; }
    int64_t start_time() const { return start_time_; }
    int64_t retimed_igt_time() const { return retimed_igt_time_; }

    // Timing calculations (exact mirrors of InGameTimer.java)
    int64_t get_real_time_attack(int64_t current_ms = -1) const;
    int64_t get_in_game_time(bool smooth = true, int64_t current_ms = -1);
    int64_t get_retimed_in_game_time(int64_t current_ms = -1);

    // Formatting (exact mirrors of TimerDrawer.java)
    std::pair<int64_t, bool> get_display_igt(bool auto_retime_display = true, int64_t current_ms = -1);
    std::wstring format_igt(int decimals, bool simply, bool auto_retime_display = true, int64_t current_ms = -1);
    std::wstring format_rta(int decimals, bool simply, int64_t current_ms = -1) const;

    // Transitions (driven by Hermes state.json & world_logs)
    void on_world_enter(const std::wstring& world_path, const std::wstring& world_name, int64_t timestamp_ms = -1);
    void on_world_leave(int64_t timestamp_ms = -1);
    void set_pause(bool to_pause, TimerStatus to_status = TimerStatus::PAUSED, int64_t current_ms = -1);

    // Authoritative snapping (SpeedRunIGT record.json & events.log)
    void snap_from_record(const json::Value& record, int64_t observed_ms = -1);
    void snap_from_event(const std::string& event_id, int64_t rta_ms, int64_t retimed_igt_ms, int64_t observed_ms = -1);

private:
    void advance_simulated_ticks(int64_t current_ms);
    void set_exact_igt(int64_t exact_igt_ms, int64_t anchor_ms);
    bool needs_auto_retime() const;

    std::wstring world_path_;
    std::wstring world_name_;
    std::string category_ = "ANY";
    std::string run_type_ = "random_seed";
    TimerStatus status_ = TimerStatus::NONE;
    bool is_completed_ = false;
    bool is_rta_mode_ = false;

    int64_t start_time_ = 0;
    int64_t end_time_ = 0;
    int64_t end_igt_time_ = 0;
    int64_t retimed_igt_time_ = 0;
    int64_t rebase_ig_time_ = 0;
    int64_t excluded_rta_ = 0;
    int64_t excluded_igt_ = 0;
    int64_t least_tick_time_ = 0;
    int64_t least_start_time_ = 0;
    int64_t activate_ticks_ = 0;
    int64_t logger_ticks_ = 0;

    int64_t logger_paused_time_ = 0;
    int64_t least_pause_time_ = 0;
    int64_t total_pause_time_ = 0;
    int pause_count_ = 0;
    int64_t leave_time_ = 0;

    int64_t completed_igt_ = 0;
    int64_t completed_retimed_igt_ = 0;
    int64_t completed_rta_ = 0;

    SnapInfo last_snap_;
};

} // namespace indeigt
