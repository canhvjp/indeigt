#include "hermes_watcher.hpp"
#include "json_mini.hpp"
#include "mc_detector.hpp"
#include "timer_engine.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace indeigt;

static void test_formatting() {
    assert(time_to_string_format(0) == L"00:00.000");
    assert(time_to_string_format(12345) == L"00:12.345");
    assert(time_to_string_format(3661042) == L"1:01:01.042");

    assert(get_time_format(12345, 0, false) == L"00:12");
    assert(get_time_format(12345, 1, false) == L"00:12.3");
    assert(get_time_format(12345, 2, false) == L"00:12.34");
    assert(get_time_format(12345, 3, false) == L"00:12.345");
    // Paused or completed always shows 3 decimals (exact SpeedRunIGT TimerDrawer rule)
    assert(get_time_format(12345, 1, true) == L"00:12.345");
}

static void test_timer_math_and_snapping() {
    SimulatedInGameTimer t;
    t.on_world_enter(L"C:\\mc\\saves\\w1", L"w1", 1000);
    assert(t.status() == TimerStatus::IDLE);
    assert(t.get_real_time_attack(1500) == 0);
    assert(t.get_in_game_time(true, 1500) == 0);

    t.set_pause(false, TimerStatus::PAUSED, 2000);
    assert(t.status() == TimerStatus::RUNNING);
    assert(t.get_real_time_attack(3234) == 1234);
    assert(t.get_in_game_time(true, 3234) == 1234);
    assert(t.get_in_game_time(false, 3234) == 1200);

    // Pause for 5000ms
    t.set_pause(true, TimerStatus::PAUSED, 3234);
    assert(t.get_in_game_time(true, 8234) == 1200);
    assert(t.get_real_time_attack(8234) == 6234);

    // Unpause -> retimed adds 5000ms
    t.set_pause(false, TimerStatus::PAUSED, 8234);
    assert(t.retimed_igt_time() == 5000);
    assert(t.get_in_game_time(true, 9234) == 2200);
    assert(t.get_retimed_in_game_time(9234) == 7200);

    // Snap from record.json
    json::Value rec;
    assert(json::parse(
        R"({"date":20100,"final_igt":9850,"final_rta":10042,"retimed_igt":9850,"is_completed":false,"category":"ANY","run_type":"random_seed","world_name":"w1"})",
        rec));
    t.set_pause(true, TimerStatus::PAUSED, 20100);
    t.snap_from_record(rec, 20105);
    assert(t.get_in_game_time(false, 20105) == 9850);
    assert(t.get_real_time_attack(20100) == 10042);
    assert(t.start_time() == 10058);

    // Snap from events.log (after 900ms pause -> retimed_igt_time = 900, so raw IGT = 14200 - 900 = 13300)
    t.set_pause(false, TimerStatus::PAUSED, 21000);
    t.snap_from_event("rsg.enter_nether", 15000, 14200, 25058);
    assert(t.get_real_time_attack(25058) == 15000);
    assert(t.get_retimed_in_game_time(25058) == 14200);
    assert(t.get_in_game_time(false, 25058) == 13300);
}

static void test_aatool_cmdline_parsing() {
    // 1. Quoted --gameDir
    std::wstring cmd1 = LR"("C:\Java\javaw.exe" -Xmx4G -Djava.library.path=C:\foo\natives --gameDir "C:\Users\Admin\AppData\Roaming\PrismLauncher\instances\1.16.1\.minecraft" --version 1.16.1)";
    assert(parse_dot_minecraft_from_cmdline(cmd1) == LR"(C:\Users\Admin\AppData\Roaming\PrismLauncher\instances\1.16.1\.minecraft)");

    // 2. Unquoted --gameDir
    std::wstring cmd2 = LR"(javaw.exe --gameDir D:/MC/1.16.1/.minecraft --assetsDir D:/MC/assets)";
    assert(parse_dot_minecraft_from_cmdline(cmd2) == LR"(D:\MC\1.16.1\.minecraft)");

    // 3. Fallback -Djava.library.path= (MultiMC / Prism style)
    std::wstring cmd3 = LR"(javaw.exe -Djava.library.path="D:\MultiMC\instances\RSG1\natives" net.minecraft.client.main.Main)";
    assert(parse_dot_minecraft_from_cmdline(cmd3) == LR"(D:\MultiMC\instances\RSG1\.minecraft)");

    // 4. Title version extraction
    assert(extract_version_from_title(L"Minecraft* 1.16.1 - Singleplayer") == L"1.16.1");
}

static void test_hermes_watcher_e2e() {
    fs::path tmp = fs::temp_directory_path() / L"indeigt_cpp_test_tmp";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::path hermes_dir = tmp / L"hermes";
    fs::path world_dir = tmp / L"saves" / L"Random Speedrun #1";
    fs::path srigt_dir = world_dir / L"speedrunigt";
    fs::create_directories(hermes_dir);
    fs::create_directories(srigt_dir);

    SimulatedInGameTimer timer;
    HermesSpeedRunWatcher watcher(timer);
    watcher.set_manual_game_dir(tmp.wstring());

    auto write_text = [](const fs::path& p, const std::string& s) {
        std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
        ofs << s;
    };

    // Enter world (loading screen -> IDLE)
    write_text(hermes_dir / L"state.json",
               R"({"screen":{"class":"net.minecraft.class_434","is_pause":false},"world":{"relative":true,"path":"saves/Random Speedrun #1"},"open_to_lan":false})");
    watcher.poll(10000);
    assert(timer.status() == TimerStatus::IDLE);

    // In-game -> RUNNING
    write_text(hermes_dir / L"state.json",
               R"({"screen":{"class":null,"is_pause":false},"world":{"relative":true,"path":"saves/Random Speedrun #1"},"open_to_lan":false})");
    watcher.poll(11000);
    assert(timer.status() == TimerStatus::RUNNING);
    assert(timer.get_in_game_time(true, 12500) == 1500);

    // Pause while open_to_lan=true -> stays RUNNING
    write_text(hermes_dir / L"state.json",
               R"({"screen":{"class":"net.minecraft.class_433","is_pause":true},"world":{"relative":true,"path":"saves/Random Speedrun #1"},"open_to_lan":true})");
    watcher.poll(13000);
    assert(timer.status() == TimerStatus::RUNNING);

    // Pause while open_to_lan=false + record.json snap
    write_text(hermes_dir / L"state.json",
               R"({"screen":{"class":"net.minecraft.class_433","is_pause":true},"world":{"relative":true,"path":"saves/Random Speedrun #1"},"open_to_lan":false})");
    write_text(srigt_dir / L"record.json",
               R"({"date":14000,"final_igt":2900,"final_rta":3000,"retimed_igt":2900,"is_completed":false,"category":"ANY","run_type":"random_seed","world_name":"Random Speedrun #1"})");
    watcher.poll(14010);
    assert(timer.status() == TimerStatus::PAUSED);
    assert(timer.get_in_game_time(true, 18000) == 2900);
    assert(timer.get_real_time_attack(18000) == 7000);

    // SeedQueue WallScreen -> resets to NONE even if world field hasn't cleared yet
    write_text(hermes_dir / L"state.json",
               R"({"screen":{"class":"me.contaria.seedqueue.gui.wall.SeedQueueWallScreen","is_pause":false},"world":{"relative":true,"path":"saves/Random Speedrun #1"},"open_to_lan":false})");
    watcher.poll(19000);
    assert(timer.status() == TimerStatus::NONE);

    // FastReset + SeedQueue instant jump into new world #2 with screen=null -> immediately RUNNING at 0ms
    fs::create_directories(tmp / L"saves" / L"Random Speedrun #2" / L"speedrunigt");
    write_text(hermes_dir / L"state.json",
               R"({"screen":{"class":null,"is_pause":false},"world":{"relative":true,"path":"saves/Random Speedrun #2"},"open_to_lan":false})");
    watcher.poll(20000);
    assert(timer.status() == TimerStatus::RUNNING);
    assert(timer.get_in_game_time(true, 20500) == 500);

    fs::remove_all(tmp, ec);
}

int main() {
    test_formatting();
    test_timer_math_and_snapping();
    test_aatool_cmdline_parsing();
    test_hermes_watcher_e2e();
    std::cout << "ALL C++ TESTS PASSED!\n";
    return 0;
}
