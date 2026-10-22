@echo off
setlocal
REM ==========================================================================
REM  Silent Hill PC Port - build the Randomizer launcher (SilentHillPC_Randomizer.exe)
REM
REM  The Randomizer launcher is a .NET Framework 4.7.2 WinForms app, built with MSBuild,
REM  separately from the game (build.bat / CMake never touch it). This finds
REM  MSBuild through vswhere, builds Release, and copies the exe into
REM  pc_port\build\ next to SilentHillPC.exe.
REM
REM  PREREQUISITE (one-time), either of:
REM    - Visual Studio 2019/2022 with the ".NET desktop development" workload
REM    - Build Tools for Visual Studio (free), workload ".NET desktop build tools":
REM        https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
REM  The csc.exe that ships with Windows (C:\Windows\Microsoft.NET\...) is too
REM  old for this code (C# 5); MSBuild from Visual Studio is required.
REM
REM  Override detection with:  set MSBUILD=C:\path\to\MSBuild.exe
REM
REM  Usage: build_randomizer_launcher.bat [output dir] [nofail]
REM    output dir  where to copy the exe (default pc_port\build)
REM    nofail      report problems but exit 0 (the CMake build step uses this,
REM                so a machine without Build Tools still builds the game)
REM ==========================================================================

set "PCPORT_DIR=%~dp0"
if "%PCPORT_DIR:~-1%"=="\" set "PCPORT_DIR=%PCPORT_DIR:~0,-1%"
set "PROJ=%PCPORT_DIR%\launcher\SilentHillPC_Remix\SilentHillPC_Remix.csproj"
set "OUT_EXE=%PCPORT_DIR%\launcher\SilentHillPC_Remix\bin\Release\SilentHillPC_Randomizer.exe"
set "BUILD_DIR=%PCPORT_DIR%\build"
if not "%~1"=="" set "BUILD_DIR=%~1"
set "NOFAIL="
if /i "%~2"=="nofail" set "NOFAIL=1"

if defined MSBUILD if exist "%MSBUILD%" goto have_msbuild

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio / Build Tools not found ^(no vswhere.exe^).
    echo Install "Build Tools for Visual Studio" with the ".NET desktop build tools" workload:
    echo   https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
    echo or set MSBUILD to the full path of MSBuild.exe.
    goto fail
)
set "MSBUILD="
for /f "usebackq delims=" %%M in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    if not defined MSBUILD set "MSBUILD=%%M"
)
if not defined MSBUILD (
    echo ERROR: Visual Studio is installed but MSBuild was not found.
    echo Add the ".NET desktop build tools" workload in the Visual Studio Installer.
    goto fail
)

:have_msbuild
echo === Building the Randomizer launcher with "%MSBUILD%" ===
REM No lingering MSBuild worker nodes or VBCSCompiler server: either keeps the
REM console/pipe handles of whoever started the build open after MSBuild exits,
REM so a build step that waits for its output to close (ninja) never finishes.
set "MSBUILDDISABLENODEREUSE=1"
REM Files from a downloaded zip carry the "mark of the web", and MSBuild then
REM refuses the .resx files (MSB3821). Clear it on the launcher sources first.
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -LiteralPath '%PCPORT_DIR%launcher' -Recurse -File | Unblock-File" >nul 2>&1
"%MSBUILD%" "%PROJ%" /nologo /v:minimal /nodeReuse:false /p:UseSharedCompilation=false /p:Configuration=Release /p:Platform=AnyCPU
if errorlevel 1 (
    echo Randomizer launcher build failed.
    echo If it reports a missing .NET Framework 4.7.2 targeting pack, add
    echo ".NET Framework 4.7.2 targeting pack" in the Visual Studio Installer.
    goto fail
)
if not exist "%OUT_EXE%" (
    echo ERROR: MSBuild finished but "%OUT_EXE%" was not produced.
    goto fail
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
REM It can't be overwritten while it is open.
tasklist /fi "imagename eq SilentHillPC_Randomizer.exe" 2>nul | find /i "SilentHillPC_Randomizer.exe" >nul
if not errorlevel 1 (
    echo ERROR: SilentHillPC_Randomizer.exe is running - close it, then run this again.
    goto fail
)
copy /y "%OUT_EXE%" "%BUILD_DIR%\" >nul
if errorlevel 1 ( echo ERROR: could not copy the launcher into "%BUILD_DIR%". & goto fail )
echo Randomizer launcher: %BUILD_DIR%\SilentHillPC_Randomizer.exe
endlocal & exit /b 0

:fail
if defined NOFAIL (
    echo WARNING: SilentHillPC_Randomizer.exe was not built - the game itself is fine.
    endlocal & exit /b 0
)
endlocal & exit /b 1
