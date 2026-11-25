#pragma once

#include "mc_detector.hpp"
#include "timer_engine.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace indeigt {

class HermesSpeedRunWatcher {
public:
    explicit HermesSpeedRunWatcher(SimulatedInGameTimer& timer);

    void set_manual_game_dir(const std::wstring& game_dir);
    void force_resync();
    void poll(int64_t current_ms = -1);

    bool is_minecraft_focused() const;
    bool has_active_instance() const { return has_active_; }
    const DetectedMinecraft& active_instance() const { return active_; }
    const std::wstring& connected_source() const { return connected_source_; }

private:
    void refresh_instance(int64_t ts);
    void poll_world_log(int64_t ts);
    void poll_state_json(int64_t ts);
    void apply_hermes_state(const json::Value& state, int64_t ts);
    void switch_world(const std::filesystem::path& world_path, int64_t ts);
    void poll_record_json(const std::filesystem::path& world_path, int64_t ts);
    void poll_events_log(const std::filesystem::path& world_path, int64_t ts);
    void poll_speedrunigt_global_fallback(int64_t ts);

    SimulatedInGameTimer& timer_;
    std::wstring manual_game_dir_;

    bool has_active_ = false;
    DetectedMinecraft active_;
    std::vector<DetectedMinecraft> all_instances_;
    std::wstring connected_source_ = L"Searching for Minecraft...";

    int64_t last_discovery_ms_ = 0;
    DWORD last_fg_pid_ = 0;
    int64_t state_mtime_ = -1;
    uintmax_t state_size_ = 0;
    std::string last_state_raw_;
    std::filesystem::path current_world_path_;
    int64_t record_mtime_ = -1;
    uintmax_t record_size_ = 0;
    std::filesystem::path events_path_;
    uintmax_t events_offset_ = 0;
    std::filesystem::path world_log_path_;
    uintmax_t world_log_offset_ = 0;
};

} // namespace indeigt
