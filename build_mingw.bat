@echo off
rem ---------------------------------------------------------------------------
rem  MinGW-w64 build (for development / Windows 7+ smoke testing).
rem  NOTE: a binary produced by MinGW links against the Universal CRT, which does
rem  not exist on Windows XP.  For a real XP build use build_msvc.bat with the
rem  static CRT (/MT) and a 32-bit toolset.
rem ---------------------------------------------------------------------------
setlocal
cd /d "%~dp0"

if not exist build mkdir build

set CFLAGS=-mwindows -municode -O2 -Wall -Wextra -Wno-unused-parameter -DUNICODE -D_UNICODE ^
 -DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -Isrc

gcc %CFLAGS% -o build\selection-menu.exe src\main.c src\config.c src\util.c ^
    src\icons.c src\hotkey.c src\detect.c src\uia.c src\popup.c src\tray.c src\settings.c ^
    src\app.rc -luser32 -lgdi32 -lshell32 -ladvapi32 -lcomctl32 -lcomdlg32 ^
    -lole32 -loleaut32 -loleacc -lm

if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo.
echo OK -^> build\selection-menu.exe
endlocal

