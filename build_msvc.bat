@echo off
rem ---------------------------------------------------------------------------
rem  Microsoft Visual C++ build - this is the one that produces a binary which
rem  really runs on Windows XP.
rem    * 32-bit toolset (x86) is recommended
rem    * /MT  = static CRT, no VC++ redistributable needed
rem ---------------------------------------------------------------------------
setlocal
cd /d "%~dp0"

set VCVARS=
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
)
if not defined VCVARS set VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat
if not exist "%VCVARS%" set VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
if not exist "%VCVARS%" set VCVARS=%ProgramFiles%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat
if not exist "%VCVARS%" (
    echo Could not find vcvarsall.bat - open a "Developer Command Prompt" instead.
    exit /b 1
)

call "%VCVARS%" x86 || exit /b 1

if not exist build mkdir build

set CFLAGS=/nologo /W3 /O2 /MT /GS- /DUNICODE /D_UNICODE /DWINVER=0x0501 /D_WIN32_WINNT=0x0501 /Isrc

rc.exe /nologo /fo build\app.res src\app.rc
if errorlevel 1 (
    echo RESOURCE COMPILATION FAILED
    exit /b 1
)

cl %CFLAGS% /Fe:build\selection-menu.exe ^
   src\main.c src\config.c src\util.c src\icons.c src\hotkey.c ^
   src\detect.c src\uia.c src\popup.c src\tray.c src\settings.c build\app.res ^
   /link /SUBSYSTEM:WINDOWS kernel32.lib user32.lib gdi32.lib shell32.lib advapi32.lib comctl32.lib comdlg32.lib ole32.lib oleaut32.lib oleacc.lib

if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo.
echo OK -^> build\selection-menu.exe
endlocal
