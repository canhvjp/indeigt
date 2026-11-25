#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace indeigt {

struct DetectedMinecraft {
    DWORD pid = 0;
    HWND hwnd = nullptr;
    std::wstring window_title;
    std::wstring game_version;
    std::wstring game_dir;
    std::wstring world_log_path;
    bool is_foreground = false;
    bool from_hermes = false;
};

// Pure helper (unit-testable): parses --gameDir or -Djava.library.path= exactly like AATool ActiveInstance.cs
std::wstring parse_dot_minecraft_from_cmdline(const std::wstring& cmdline);

// Extract game version from Minecraft window title (e.g. "Minecraft* 1.16.1 - Singleplayer" -> "1.16.1")
std::wstring extract_version_from_title(const std::wstring& title);

// Read a process's command line via NtQueryInformationProcess + PEB ReadProcessMemory
std::wstring read_process_command_line(DWORD pid);

// Check if a Windows PID is still alive
bool is_process_alive(DWORD pid);

// Get HermesCore global folder (%LOCALAPPDATA%/MCSRHermes -> %APPDATA%/MCSRHermes -> %USERPROFILE%/MCSRHermes)
std::wstring get_global_hermes_folder();

// Get SpeedRunIGT global folder (%APPDATA%/speedrunigt)
std::wstring get_speedrunigt_global_folder();

// Check [game_dir]/hermes/alive 16-byte big-endian (pid, timestamp_ms)
bool check_hermes_alive_file(const std::wstring& game_dir, int64_t expected_pid, int64_t current_ms);

// Detect all running Minecraft instances (AATool process/window scan + HermesCore [PID].json)
std::vector<DetectedMinecraft> detect_minecraft_instances(int64_t current_ms);

} // namespace indeigt
