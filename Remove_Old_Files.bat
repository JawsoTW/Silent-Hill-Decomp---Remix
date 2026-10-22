@echo off
REM Deletes files this drop-in no longer uses, so the game's CMake does not
REM build them in and nothing old is left to confuse:
REM   - the port's old randomizer (the police-station roguelike) and its Lua
REM     scripting: the randomizer is now the add-on in pc_port\addons\remix
REM   - what earlier drop-ins left: remix sources in the game's own folders
REM     (before 17e: "multiple definition of Pc_Remix_Init"), 17f-17r's separate
REM     add-on build (its remix_sym.c would be compiled into the add-on), and the
REM     launcher build script, add-on and launcher under their old Remix names
REM
REM Usage: copy this file to your repo root (the folder holding pc_port\ and
REM src\) and run it there, or run it with the repo root as its argument.
REM Your settings (pc_port\build\remix.cfg) are kept: the game makes
REM randomizer.cfg from it on its next start.
setlocal
set "ROOT=%~1"
if "%ROOT%"=="" set "ROOT=%CD%"
if not exist "%ROOT%\pc_port\src" (
    echo pc_port\src not found under "%ROOT%". Run this from the repo root.
    exit /b 1
)
for %%F in (
    "pc_port\src\pc_rando.c"
    "pc_port\src\pc_rando_config.c"
    "pc_port\src\pc_rando_lua.c"
    "pc_port\src\pc_rando_settings.c"
    "pc_port\include\pc_rando.h"
    "pc_port\include\pc_rando_config.h"
    "pc_port\include\pc_rando_data.h"
    "pc_port\include\pc_rando_lua.h"
    "pc_port\include\pc_rando_settings.h"
    "pc_port\tools\gen_rando_data.py"
    "pc_port\docs\Randomizer_Scripting.md"
    "pc_port\docs\archive\Randomizer_Mode_Design.md"
    "pc_port\docs\Remix_Mode.md"
    "pc_port\build_remix_launcher.bat"
    "pc_port\build\SilentHillPC_RemixAddon.dll"
    "pc_port\build\SilentHillPC_RemixAddon.so"
    "pc_port\build\SilentHillPC_Remix.exe"
    "pc_port\src\pc_remix.c"
    "pc_port\src\pc_remix_layout.c"
    "pc_port\src\pc_remix_sfx.c"
    "pc_port\include\pc_remix_data.h"
    "pc_port\include\pc_remix_layout.h"
    "pc_port\include\pc_remix_sfx_data.h"
    "pc_port\launcher\SilentHillPC_Launcher\RemixSettingsForm.cs"
    "pc_port\addons\remix\src\remix_sym.c"
    "pc_port\addons\remix\build_remix.bat"
    "pc_port\addons\remix\build_remix_launcher.bat"
    "pc_port\addons\remix\apply_remix_hooks.bat"
    "pc_port\addons\remix\remix_imports.def"
    "pc_port\addons\remix\include\remix_engine.h"
) do (
    if exist "%ROOT%\%%~F" (
        del /q "%ROOT%\%%~F" && echo removed %%~F
    )
)
for %%D in (
    "pc_port\third_party\lua"
    "pc_port\launcher\SilentHillPC_Remix\bin"
    "pc_port\launcher\SilentHillPC_Remix\obj"
    "pc_port\addons\remix\sdk"
    "pc_port\addons\remix\hooks"
    "pc_port\addons\remix\tools"
    "pc_port\addons\remix\tests"
    "pc_port\addons\remix\docs"
    "pc_port\addons\remix\launcher"
    "pc_port\addons\remix\build"
) do (
    if exist "%ROOT%\%%~D" (
        rmdir /s /q "%ROOT%\%%~D" && echo removed %%~D\
    )
)
echo Done. Now copy the drop-in's folders in, then run: pc_port\build.bat rebuild
endlocal
