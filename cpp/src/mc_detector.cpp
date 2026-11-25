#include "mc_detector.hpp"
#include "json_mini.hpp"

#include <winternl.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace indeigt {

namespace {

using NtQueryInformationProcessFn = NTSTATUS(NTAPI*)(
    HANDLE ProcessHandle,
    PROCESSINFOCLASS ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength);

std::wstring utf8_to_wide_local(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

std::wstring get_env_wide(const wchar_t* name) {
    DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return {};
    std::wstring buf(needed, L'\0');
    DWORD written = GetEnvironmentVariableW(name, buf.data(), needed);
    buf.resize(written);
    return buf;
}

std::wstring to_lower_copy(std::wstring s) {
    for (auto& ch : s) ch = static_cast<wchar_t>(std::towlower(ch));
    return s;
}

bool starts_with_ci(const std::wstring& text, const std::wstring& prefix) {
    if (text.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::towlower(text[i]) != std::towlower(prefix[i])) return false;
    }
    return true;
}

std::wstring get_process_base_name(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    wchar_t buf[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    std::wstring result;
    if (QueryFullProcessImageNameW(h, 0, buf, &size)) {
        std::wstring full(buf, size);
        size_t pos = full.find_last_of(L"\\/");
        result = (pos == std::wstring::npos) ? full : full.substr(pos + 1);
    }
    CloseHandle(h);
    return result;
}

std::wstring resolve_hermes_path_obj(const std::wstring& game_dir, const json::Value* path_obj) {
    if (!path_obj || !path_obj->is_object()) return {};
    const auto* p = path_obj->get("path");
    if (!p || !p->is_string() || p->str_val.empty()) return {};
    std::wstring raw = utf8_to_wide_local(p->str_val);
    bool rel = path_obj->get("relative") ? path_obj->get("relative")->as_bool(false) : false;
    fs::path fp = rel ? (fs::path(game_dir) / raw) : fs::path(raw);
    return fp.lexically_normal().wstring();
}

} // namespace

bool is_process_alive(DWORD pid) {
    if (pid == 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD code = 0;
    bool alive = GetExitCodeProcess(h, &code) && (code == STILL_ACTIVE);
    CloseHandle(h);
    return alive;
}

std::wstring read_process_command_line(DWORD pid) {
    if (pid == 0) return {};
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return {};

    std::wstring cmdline;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto nt_query = ntdll ? reinterpret_cast<NtQueryInformationProcessFn>(GetProcAddress(ntdll, "NtQueryInformationProcess")) : nullptr;

    if (nt_query) {
        PROCESS_BASIC_INFORMATION pbi{};
        ULONG ret_len = 0;
        if (NT_SUCCESS(nt_query(hProc, ProcessBasicInformation, &pbi, sizeof(pbi), &ret_len)) && pbi.PebBaseAddress) {
            PEB peb{};
            SIZE_T bytes_read = 0;
            if (ReadProcessMemory(hProc, pbi.PebBaseAddress, &peb, sizeof(peb), &bytes_read) && peb.ProcessParameters) {
                RTL_USER_PROCESS_PARAMETERS params{};
                if (ReadProcessMemory(hProc, peb.ProcessParameters, &params, sizeof(params), &bytes_read)) {
                    USHORT len_bytes = params.CommandLine.Length;
                    if (len_bytes > 0 && len_bytes < 65500 && params.CommandLine.Buffer) {
                        cmdline.resize(len_bytes / sizeof(wchar_t));
                        if (!ReadProcessMemory(hProc, params.CommandLine.Buffer, cmdline.data(), len_bytes, &bytes_read)) {
                            cmdline.clear();
                        }
                    }
                }
            }
        }
    }
    CloseHandle(hProc);
    return cmdline;
}

std::wstring parse_dot_minecraft_from_cmdline(const std::wstring& cmdline) {
    if (cmdline.empty()) return {};

    // 1. Primary AATool method: --gameDir "path" or --gameDir path
    const std::wstring game_dir_flag = L"--gameDir ";
    size_t pos = cmdline.find(game_dir_flag);
    if (pos != std::wstring::npos) {
        size_t i = pos + game_dir_flag.size();
        while (i < cmdline.size() && cmdline[i] == L' ') ++i;
        std::wstring path;
        if (i < cmdline.size() && cmdline[i] == L'"') {
            ++i;
            size_t end = cmdline.find(L'"', i);
            if (end != std::wstring::npos) path = cmdline.substr(i, end - i);
        } else {
            size_t end = cmdline.find(L' ', i);
            path = (end == std::wstring::npos) ? cmdline.substr(i) : cmdline.substr(i, end - i);
        }
        if (!path.empty()) {
            std::replace(path.begin(), path.end(), L'/', L'\\');
            while (path.size() > 3 && path.back() == L'\\') path.pop_back();
            return path;
        }
    }

    // 2. Alternate AATool method: -Djava.library.path=...natives (adjacent to .minecraft or minecraft)
    const std::wstring natives_flag = L"-Djava.library.path=";
    pos = cmdline.find(natives_flag);
    if (pos != std::wstring::npos) {
        bool quoted = (pos > 0 && cmdline[pos - 1] == L'"');
        size_t i = pos + natives_flag.size();
        if (i < cmdline.size() && cmdline[i] == L'"') {
            quoted = true;
            ++i;
        }
        size_t end = quoted ? cmdline.find(L'"', i) : cmdline.find(L' ', i);
        std::wstring natives_path = (end == std::wstring::npos) ? cmdline.substr(i) : cmdline.substr(i, end - i);
        std::replace(natives_path.begin(), natives_path.end(), L'/', L'\\');
        while (natives_path.size() > 3 && natives_path.back() == L'\\') natives_path.pop_back();

        fs::path np(natives_path);
        fs::path parent = np.parent_path();
        if (!parent.empty()) {
            fs::path dot_mc = parent / L".minecraft";
            fs::path mc = parent / L"minecraft";
            std::error_code ec;
            if (fs::is_directory(dot_mc, ec)) return dot_mc.wstring();
            if (fs::is_directory(mc, ec)) return mc.wstring();
            return dot_mc.wstring();
        }
    }
    return {};
}

std::wstring extract_version_from_title(const std::wstring& title) {
    // e.g., "Minecraft* 1.16.1 - Singleplayer" -> "1.16.1"
    std::wistringstream iss(title);
    std::wstring first, second;
    if (iss >> first >> second) {
        return second;
    }
    return {};
}

std::wstring get_global_hermes_folder() {
    for (const wchar_t* env : {L"LOCALAPPDATA", L"APPDATA", L"USERPROFILE"}) {
        std::wstring base = get_env_wide(env);
        if (!base.empty()) {
            return (fs::path(base) / L"MCSRHermes").wstring();
        }
    }
    return L"MCSRHermes";
}

std::wstring get_speedrunigt_global_folder() {
    std::wstring appdata = get_env_wide(L"APPDATA");
    if (!appdata.empty()) {
        return (fs::path(appdata) / L"speedrunigt").wstring();
    }
    return L"speedrunigt";
}

bool check_hermes_alive_file(const std::wstring& game_dir, int64_t expected_pid, int64_t current_ms) {
    fs::path alive_path = fs::path(game_dir) / L"hermes" / L"alive";
    std::ifstream ifs(alive_path, std::ios::binary);
    if (!ifs) return false;
    uint8_t buf[16] = {};
    ifs.read(reinterpret_cast<char*>(buf), 16);
    if (ifs.gcount() < 16) return false;

    auto read_be64 = [](const uint8_t* p) -> int64_t {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
        return static_cast<int64_t>(v);
    };
    int64_t file_pid = read_be64(buf);
    int64_t ts_ms = read_be64(buf + 8);
    if (expected_pid > 0 && file_pid != expected_pid) return false;
    if (ts_ms <= 0) return false;
    int64_t diff = current_ms - ts_ms;
    if (diff < 0) diff = -diff;
    return diff < 15000;
}

std::vector<DetectedMinecraft> detect_minecraft_instances(int64_t current_ms) {
    std::map<DWORD, DetectedMinecraft> by_pid;
    HWND fg_hwnd = GetForegroundWindow();
    DWORD fg_pid = 0;
    if (fg_hwnd) GetWindowThreadProcessId(fg_hwnd, &fg_pid);

    // 1. AATool-style scan of all visible top-level windows (java* process + title starting with "Minecraft")
    struct EnumCtx {
        std::map<DWORD, DetectedMinecraft>* by_pid;
        DWORD fg_pid;
    } ctx{&by_pid, fg_pid};

    EnumWindows([](HWND hwnd, LPARAM lparam) -> BOOL {
        if (!IsWindowVisible(hwnd)) return TRUE;
        wchar_t title_buf[512] = {};
        int len = GetWindowTextW(hwnd, title_buf, 512);
        if (len <= 0) return TRUE;
        std::wstring title(title_buf, len);
        if (!starts_with_ci(title, L"Minecraft")) return TRUE;

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0) return TRUE;

        std::wstring proc_name = get_process_base_name(pid);
        if (!starts_with_ci(proc_name, L"java")) return TRUE;

        auto* c = reinterpret_cast<EnumCtx*>(lparam);
        DetectedMinecraft& info = (*c->by_pid)[pid];
        info.pid = pid;
        info.hwnd = hwnd;
        info.window_title = title;
        info.game_version = extract_version_from_title(title);
        info.is_foreground = (pid == c->fg_pid);

        if (info.game_dir.empty()) {
            std::wstring cmdline = read_process_command_line(pid);
            info.game_dir = parse_dot_minecraft_from_cmdline(cmdline);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    // 2. HermesCore [PID].json discovery (merges world_log_path and catches instances even if title is customized)
    fs::path inst_dir = fs::path(get_global_hermes_folder()) / L"instances";
    std::error_code ec;
    if (fs::is_directory(inst_dir, ec)) {
        for (const auto& entry : fs::directory_iterator(inst_dir, ec)) {
            if (!entry.is_regular_file(ec) || entry.path().extension() != L".json") continue;
            std::ifstream ifs(entry.path(), std::ios::binary);
            if (!ifs) continue;
            std::ostringstream oss;
            oss << ifs.rdbuf();
            json::Value root;
            if (!json::parse(oss.str(), root) || !root.is_object()) continue;

            DWORD pid = static_cast<DWORD>(root.get("pid") ? root.get("pid")->as_int64(0) : 0);
            if (pid == 0 || !is_process_alive(pid)) continue;

            std::wstring gdir = root.get("game_dir") ? utf8_to_wide_local(root.get("game_dir")->as_string()) : L"";
            if (gdir.empty()) continue;

            bool alive_ok = check_hermes_alive_file(gdir, pid, current_ms);
            if (!alive_ok && !fs::exists(fs::path(gdir) / L"hermes" / L"state.json", ec)) {
                continue;
            }

            DetectedMinecraft& info = by_pid[pid];
            info.pid = pid;
            info.game_dir = gdir;
            info.from_hermes = true;
            info.is_foreground = (pid == fg_pid);
            if (info.game_version.empty() && root.get("game_version")) {
                info.game_version = utf8_to_wide_local(root.get("game_version")->as_string());
            }
            std::wstring wlog = resolve_hermes_path_obj(gdir, root.get("world_log"));
            if (!wlog.empty()) info.world_log_path = wlog;
        }
    }

    std::vector<DetectedMinecraft> result;
    result.reserve(by_pid.size());
    for (auto& [pid, info] : by_pid) {
        if (!info.game_dir.empty()) {
            result.push_back(std::move(info));
        }
    }
    return result;
}

} // namespace indeigt
