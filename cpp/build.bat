@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%SCRIPT_DIR%build"
set "DIST_DIR=%ROOT_DIR%\dist\IndeIGT"

set "VCVARS="
for %%P in (
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) do (
    if exist %%P set "VCVARS=%%~P"
)

if "%VCVARS%"=="" (
    echo ERROR: Could not find vcvars64.bat
    exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo [1/3] Compiling IndeIGT.exe (Release x64, static CRT /MT)...
cl /nologo /utf-8 /std:c++20 /O2 /MT /EHsc /W3 /DUNICODE /D_UNICODE /DNOMINMAX ^
    /I"%SCRIPT_DIR%src" ^
    "%SCRIPT_DIR%src\main.cpp" ^
    "%SCRIPT_DIR%src\config.cpp" ^
    "%SCRIPT_DIR%src\timer_engine.cpp" ^
    "%SCRIPT_DIR%src\mc_detector.cpp" ^
    "%SCRIPT_DIR%src\hermes_watcher.cpp" ^
    "%SCRIPT_DIR%src\overlay_window.cpp" ^
    "%SCRIPT_DIR%src\settings_window.cpp" ^
    /Fo"%BUILD_DIR%\\" ^
    /Fe:"%BUILD_DIR%\IndeIGT.exe" ^
    /link /SUBSYSTEM:WINDOWS gdiplus.lib dwmapi.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1

echo [2/3] Compiling and running C++ unit tests...
cl /nologo /utf-8 /std:c++20 /O2 /MT /EHsc /W3 /DUNICODE /D_UNICODE /DNOMINMAX ^
    /I"%SCRIPT_DIR%src" ^
    "%SCRIPT_DIR%tests\test_timer.cpp" ^
    "%SCRIPT_DIR%src\timer_engine.cpp" ^
    "%SCRIPT_DIR%src\mc_detector.cpp" ^
    "%SCRIPT_DIR%src\hermes_watcher.cpp" ^
    /Fo"%BUILD_DIR%\\" ^
    /Fe:"%BUILD_DIR%\IndeIGT_Tests.exe" ^
    /link user32.lib
if errorlevel 1 exit /b 1

"%BUILD_DIR%\IndeIGT_Tests.exe"
if errorlevel 1 exit /b 1

echo [3/3] Packaging distributable folder: %DIST_DIR%
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
if not exist "%DIST_DIR%\fonts" mkdir "%DIST_DIR%\fonts"
copy /Y "%BUILD_DIR%\IndeIGT.exe" "%DIST_DIR%\IndeIGT.exe" >nul
echo Build and package succeeded: %DIST_DIR%\IndeIGT.exe
exit /b 0
