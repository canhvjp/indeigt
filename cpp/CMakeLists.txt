cmake_minimum_required(VERSION 3.20)
project(IndeIGT LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(IndeIGT WIN32
    src/main.cpp
    src/config.cpp
    src/timer_engine.cpp
    src/mc_detector.cpp
    src/hermes_watcher.cpp
    src/overlay_window.cpp
    src/settings_window.cpp
)

target_compile_definitions(IndeIGT PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(IndeIGT PRIVATE
    gdiplus
    dwmapi
    comctl32
    comdlg32
    shell32
    ole32
    user32
    gdi32
)

add_executable(IndeIGT_Tests
    tests/test_timer.cpp
    src/timer_engine.cpp
    src/mc_detector.cpp
    src/hermes_watcher.cpp
)

target_include_directories(IndeIGT_Tests PRIVATE src)
target_compile_definitions(IndeIGT_Tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(IndeIGT_Tests PRIVATE user32)
