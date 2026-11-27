#include "timer_engine.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace indeigt {

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::wstring time_to_string_format(int64_t time_ms) {
    if (time_ms < 0) time_ms = 0;
    int64_t total_sec = time_ms / 1000;
    int64_t seconds = total_sec % 60;
    int64_t minutes = total_sec / 60;
    int64_t ms = time_ms % 1000;
    wchar_t buf[64];
    if (minutes > 59) {
        int64_t hours = minutes / 60;
        minutes %= 60;
        std::swprintf(buf, 64, L"%lld:%02lld:%02lld.%03lld", hours, minutes, seconds, ms);
    } else {
        std::swprintf(buf, 64, L"%02lld:%02lld.%03lld", minutes, seconds, ms);
    }
    return buf;
}

std::wstring get_time_format(int64_t time_ms, int decimals, bool is_paused_or_completed) {
    if (time_ms < 0) time_ms = 0;
    if (is_paused_or_completed) {
        return time_to_string_format(time_ms);
    }
    decimals = std::clamp(decimals, 0, 3);
    int64_t total_sec = time_ms / 1000;
    int64_t seconds = total_sec % 60;
    int64_t minutes = total_sec / 60;
    int64_t ms = time_ms % 1000;

    wchar_t ms_buf[8];
    std::swprintf(ms_buf, 8, L"%03lld", ms);
    std::wstring ms_str(ms_buf, static_cast<size_t>(decimals));

    wchar_t buf[64];
    if (minutes > 59) {
        int64_t hours = minutes / 60;
        minutes %= 60;
        if (decimals == 0) {
            std::swprintf(buf, 64, L"%lld:%02lld:%02lld", hours, minutes, seconds);
        } else {
            std::swprintf(buf, 64, L"%lld:%02lld:%02lld.%ls", hours, minutes, seconds, ms_str.c_str());
        }
    } else {
        if (decimals == 0) {
            std::swprintf(buf, 64, L"%02lld:%02lld", minutes, seconds);
        } else {
            std::swprintf(buf, 64, L"%02lld:%02lld.%ls", minutes, seconds, ms_str.c_str());
        }
    }
    return buf;
}

SimulatedInGameTimer::SimulatedInGameTimer() {
    reset_to_none();
}

void SimulatedInGameTimer::reset_to_none() {
    world_path_.clear();
    world_name_.clear();
    category_ = "ANY";
    run_type_ = "random_seed";
    status_ = TimerStatus::NONE;
    is_completed_ = false;
    is_rta_mode_ = false;

    start_time_ = 0;
    end_time_ = 0;
    end_igt_time_ = 0;
    retimed_igt_time_ = 0;
    rebase_ig_time_ = 0;
    excluded_rta_ = 0;
    excluded_igt_ = 0;
    least_tick_time_ = 0;
    least_start_time_ = 0;
    activate_ticks_ = 0;
    logger_ticks_ = 0;

    logger_paused_time_ = 0;
    least_pause_time_ = 0;
    total_pause_time_ = 0;
    pause_count_ = 0;
    leave_time_ = 0;

    completed_igt_ = 0;
    completed_retimed_igt_ = 0;
    completed_rta_ = 0;
    last_snap_ = SnapInfo{};
}

void SimulatedInGameTimer::advance_simulated_ticks(int64_t current_ms) {
    if (!is_playing() || !is_started()) return;
    if (least_tick_time_ == 0) {
        least_tick_time_ = current_ms;
        return;
    }
    int64_t elapsed = current_ms - least_tick_time_;
    if (elapsed >= 50) {
        int64_t ticks = elapsed / 50;
        activate_ticks_ += ticks;
        logger_ticks_ += ticks;
        least_tick_time_ += ticks * 50;
    }
}

int64_t SimulatedInGameTimer::get_real_time_attack(int64_t current_ms) const {
    if (is_completed()) return std::max<int64_t>(0, completed_rta_);
    if (status_ == TimerStatus::NONE) return 0;
    if (status_ == TimerStatus::LEAVE && leave_time_ > 0 && is_started()) {
        return std::max<int64_t>(0, leave_time_ - start_time_ - excluded_rta_);
    }
    if (!is_started()) return 0;
    int64_t ts = (current_ms < 0) ? now_ms() : current_ms;
    int64_t end_t = (status_ == TimerStatus::COMPLETED_LEGACY) ? end_time_ : ts;
    return std::max<int64_t>(0, end_t - start_time_ - excluded_rta_);
}

int64_t SimulatedInGameTimer::get_in_game_time(bool smooth, int64_t current_ms) {
    if (is_completed()) return std::max<int64_t>(0, completed_igt_);
    if (is_rta_mode_) return get_real_time_attack(current_ms);
    if (!is_started()) return 0;

    int64_t ts = (current_ms < 0) ? now_ms() : current_ms;
    advance_simulated_ticks(ts);

    int64_t smooth_add = 0;
    if (smooth && is_playing() && least_tick_time_ != 0) {
        smooth_add = std::min<int64_t>(50, std::max<int64_t>(0, ts - least_tick_time_));
    }

    int64_t igt = (activate_ticks_ * 50) + smooth_add - rebase_ig_time_ - excluded_igt_ + end_igt_time_;
    return std::max<int64_t>(0, igt);
}

bool SimulatedInGameTimer::needs_auto_retime() const {
    return (category_ == "ANY" || category_ == "any") && run_type_ == "random_seed";
}

int64_t SimulatedInGameTimer::get_retimed_in_game_time(int64_t current_ms) {
    if (is_completed()) return std::max<int64_t>(0, completed_retimed_igt_);
    int64_t base = get_in_game_time(false, current_ms);
    if (needs_auto_retime()) {
        return base + retimed_igt_time_;
    }
    return base;
}

std::pair<int64_t, bool> SimulatedInGameTimer::get_display_igt(bool auto_retime_display, int64_t current_ms) {
    int64_t ts = (current_ms < 0) ? now_ms() : current_ms;
    if (is_completed() && auto_retime_display && needs_auto_retime() &&
        completed_retimed_igt_ != completed_igt_ && ((ts / 3000) % 2 == 0)) {
        return {completed_retimed_igt_, true};
    }
    return {get_in_game_time(true, ts), false};
}

std::wstring SimulatedInGameTimer::format_igt(int decimals, bool simply, bool auto_retime_display, int64_t current_ms) {
    auto [igt_ms, is_retime] = get_display_igt(auto_retime_display, current_ms);
    std::wstring formatted = get_time_format(igt_ms, decimals, is_completed() || is_paused());
    if (simply) return formatted;
    return (is_retime ? L"IGT*: " : L"IGT: ") + formatted;
}

std::wstring SimulatedInGameTimer::format_rta(int decimals, bool simply, int64_t current_ms) const {
    int64_t rta_ms = get_real_time_attack(current_ms);
    std::wstring formatted = get_time_format(rta_ms, decimals, is_completed() || is_paused());
    if (simply) return formatted;
    return L"RTA: " + formatted;
}

void SimulatedInGameTimer::on_world_enter(const std::wstring& world_path, const std::wstring& world_name, int64_t timestamp_ms) {
    if (world_path == world_path_ && status_ != TimerStatus::NONE) return;
    reset_to_none();
    world_path_ = world_path;
    world_name_ = world_name;
    status_ = TimerStatus::IDLE;
    last_snap_.source = "world_enter";
    last_snap_.timestamp_ms = (timestamp_ms < 0) ? now_ms() : timestamp_ms;
}

void SimulatedInGameTimer::on_world_leave(int64_t timestamp_ms) {
    if (status_ == TimerStatus::NONE) return;
    int64_t ts = (timestamp_ms < 0) ? now_ms() : timestamp_ms;
    advance_simulated_ticks(ts);
    leave_time_ = ts;
    pause_count_ = 0;
    if (!is_completed()) {
        status_ = TimerStatus::LEAVE;
    }
}

void SimulatedInGameTimer::set_pause(bool to_pause, TimerStatus to_status, int64_t current_ms) {
    if (is_completed() || status_ == TimerStatus::NONE) return;
    int64_t ts = (current_ms < 0) ? now_ms() : current_ms;

    if (to_pause) {
        if (pause_priority(status_) <= pause_priority(to_status)) {
            advance_simulated_ticks(ts);
            if (pause_priority(status_) < 1 && is_started()) {
                logger_paused_time_ = get_real_time_attack(ts);
                ++pause_count_;
            }
            status_ = to_status;
        }
    } else {
        if (is_started()) {
            int64_t now_rta = get_real_time_attack(ts);
            if (status_ == TimerStatus::PAUSED) {
                retimed_igt_time_ += std::max<int64_t>(0, now_rta - logger_paused_time_);
            }
            if (is_paused()) {
                least_pause_time_ = std::max<int64_t>(0, now_rta - logger_paused_time_);
                total_pause_time_ += least_pause_time_;
                leave_time_ = 0;
            }
            if (status_ == TimerStatus::IDLE && logger_ticks_ != 0) {
                least_start_time_ = ts;
            }
        } else {
            start_time_ = ts;
            if (logger_ticks_ != 0) least_start_time_ = start_time_;
        }
        least_tick_time_ = ts;
        status_ = TimerStatus::RUNNING;
    }
}

void SimulatedInGameTimer::set_exact_igt(int64_t exact_igt_ms, int64_t anchor_ms) {
    exact_igt_ms = std::max<int64_t>(0, exact_igt_ms);
    int64_t ticks = exact_igt_ms / 50;
    int64_t rem = exact_igt_ms % 50;
    if (rem > 0) {
        activate_ticks_ = ticks + 1;
        rebase_ig_time_ = 50 - rem;
    } else {
        activate_ticks_ = ticks;
        rebase_ig_time_ = 0;
    }
    excluded_igt_ = 0;
    end_igt_time_ = 0;
    least_tick_time_ = anchor_ms;
}

void SimulatedInGameTimer::snap_from_record(const json::Value& record, int64_t observed_ms) {
    if (!record.is_object()) return;
    int64_t ts = (observed_ms < 0) ? now_ms() : observed_ms;
    int64_t record_date = ts;
    if (const auto* v = record.get("date")) {
        int64_t d = v->as_int64(0);
        if (d > 0) record_date = d;
    }
    int64_t final_igt = record.get("final_igt") ? record.get("final_igt")->as_int64(0) : 0;
    int64_t final_rta = record.get("final_rta") ? record.get("final_rta")->as_int64(0) : 0;
    int64_t retimed_igt = record.get("retimed_igt") ? record.get("retimed_igt")->as_int64(final_igt) : final_igt;
    bool completed = record.get("is_completed") ? record.get("is_completed")->as_bool(false) : false;

    if (const auto* v = record.get("category"); v && v->is_string() && !v->str_val.empty()) {
        category_ = v->str_val;
    }
    if (const auto* v = record.get("run_type"); v && v->is_string() && !v->str_val.empty()) {
        run_type_ = v->str_val;
    }
    if (const auto* v = record.get("world_name"); v && v->is_string() && !v->str_val.empty()) {
        int len = MultiByteToWideChar(CP_UTF8, 0, v->str_val.c_str(), static_cast<int>(v->str_val.size()), nullptr, 0);
        world_name_.assign(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, v->str_val.c_str(), static_cast<int>(v->str_val.size()), world_name_.data(), len);
    }

    int64_t prev_igt = is_started() ? get_in_game_time(false, record_date) : 0;
    int64_t prev_rta = is_started() ? get_real_time_attack(record_date) : 0;

    if (final_rta > 0 || completed || final_igt > 0) {
        start_time_ = record_date - final_rta;
        excluded_rta_ = 0;
    }
    retimed_igt_time_ = std::max<int64_t>(0, retimed_igt - final_igt);

    if (completed) {
        is_completed_ = true;
        status_ = TimerStatus::COMPLETED_LEGACY;
        end_time_ = record_date;
        completed_igt_ = final_igt;
        completed_retimed_igt_ = retimed_igt;
        completed_rta_ = final_rta;
        set_exact_igt(final_igt, record_date);
    } else {
        if (is_completed_) {
            is_completed_ = false;
            if (status_ == TimerStatus::COMPLETED_LEGACY) {
                status_ = TimerStatus::RUNNING;
            }
        }
        if (is_playing() && ts > record_date) {
            set_exact_igt(final_igt, record_date);
            advance_simulated_ticks(ts);
        } else {
            set_exact_igt(final_igt, ts);
            if (status_ == TimerStatus::PAUSED) {
                logger_paused_time_ = final_rta;
            }
        }
    }

    last_snap_.source = "record.json";
    last_snap_.timestamp_ms = ts;
    last_snap_.igt_drift_ms = final_igt - prev_igt;
    last_snap_.rta_drift_ms = final_rta - prev_rta;
}

void SimulatedInGameTimer::snap_from_event(const std::string& event_id, int64_t rta_ms, int64_t retimed_igt_ms, int64_t observed_ms) {
    if (is_completed()) return;
    int64_t ts = (observed_ms < 0) ? now_ms() : observed_ms;
    rta_ms = std::max<int64_t>(0, rta_ms);
    retimed_igt_ms = std::max<int64_t>(0, retimed_igt_ms);

    int64_t raw_igt_ms = std::max<int64_t>(0, retimed_igt_ms - (needs_auto_retime() ? retimed_igt_time_ : 0));
    int64_t prev_igt = is_started() ? get_in_game_time(false, ts) : 0;
    int64_t prev_rta = is_started() ? get_real_time_attack(ts) : 0;

    if (rta_ms > 0 || raw_igt_ms > 0) {
        start_time_ = ts - rta_ms;
        excluded_rta_ = 0;
        set_exact_igt(raw_igt_ms, ts);
    }

    // Do NOT mark run completed on "rsg.credits" here:
    // SpeedRunIGT logs rsg.credits for ALL categories (including All Advancements, All Bosses, Custom, etc.).
    // Authoritative completion for ANY SpeedRunIGT category comes from speedrunigt/record.json ("is_completed": true).

    last_snap_.source = "events.log (" + event_id + ")";
    last_snap_.timestamp_ms = ts;
    last_snap_.igt_drift_ms = raw_igt_ms - prev_igt;
    last_snap_.rta_drift_ms = rta_ms - prev_rta;
}

} // namespace indeigt
