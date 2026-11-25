#include "config.hpp"
#include "hermes_watcher.hpp"
#include "overlay_window.hpp"
#include "settings_window.hpp"
#include "timer_engine.hpp"

#include <windows.h>
#include <gdiplus.h>

namespace {

void enable_dpi_awareness() {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        using SetDpiCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto fn = reinterpret_cast<SetDpiCtxFn>(GetProcAddress(u32, "SetProcessDpiAwarenessContext"));
        if (fn) {
            fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    SetProcessDPIAware();
}

} // namespace

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR, int) {
    enable_dpi_awareness();

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\IndeIGT_Overlay_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    Gdiplus::GdiplusStartupInput gdip_in{};
    ULONG_PTR gdip_token = 0;
    Gdiplus::GdiplusStartup(&gdip_token, &gdip_in, nullptr);

    {
        indeigt::OverlayConfig cfg = indeigt::OverlayConfig::load();
        indeigt::SimulatedInGameTimer timer;
        indeigt::HermesSpeedRunWatcher watcher(timer);
        if (!cfg.manual_game_dir.empty()) {
            watcher.set_manual_game_dir(cfg.manual_game_dir);
        }

        indeigt::OverlayWindow overlay(cfg, timer, watcher);
        indeigt::SettingsWindow settings(cfg, timer, watcher, overlay);

        overlay.on_config_changed = [&]() { settings.sync_controls_from_config(); };
        overlay.on_open_settings = [&]() { settings.show_and_focus(); };
        overlay.on_close_requested = [&]() { PostQuitMessage(0); };
        settings.on_quit = [&]() { PostQuitMessage(0); };

        if (!overlay.create(hinst) || !settings.create(hinst, true)) {
            Gdiplus::GdiplusShutdown(gdip_token);
            CoUninitialize();
            if (mutex) CloseHandle(mutex);
            return 1;
        }

        int64_t last_poll = 0;
        int64_t last_status = 0;
        MSG msg{};
        bool running = true;

        while (running) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!running) break;

            int64_t ts = indeigt::now_ms();
            if (ts - last_poll >= 25) {
                last_poll = ts;
                watcher.poll(ts);
            }
            overlay.render(ts);

            if (ts - last_status >= 250) {
                last_status = ts;
                settings.update_status_text();
            }

            Sleep(16); // ~60 FPS smooth timer, ~0% CPU when idle/paused thanks to dirty-frame skip
        }

        cfg.save();
    }

    Gdiplus::GdiplusShutdown(gdip_token);
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}
