# Runs build_launcher.bat for the SilentHillPC_Launcher build step.
#   cmake -DBAT=<build_launcher.bat> -DOUT_DIR=<build dir> -DSTAMP=<stamp file> -P build_launcher.cmake
#
# Started through cmake.exe rather than as `cmd /c ...` in the build rule: under
# an MSYS2 shell, MSYS rewrites a bare "/c" argument into the path "C:/", which
# leaves cmd.exe sitting at an interactive prompt and the build hangs on step 1.
# execute_process passes arguments to cmd.exe untouched. stdin comes from NUL so
# nothing can ever wait for input, and a timeout stops a stuck MSBuild from
# holding the game build forever. Never fails the build: the game doesn't need
# the launcher.

file(TO_NATIVE_PATH "${BAT}" _bat)
file(TO_NATIVE_PATH "${OUT_DIR}" _out)
if(WIN32 OR CMAKE_HOST_WIN32)
    set(_null NUL)
else()
    set(_null /dev/null)
endif()

# An MSYS-built cmake would still rewrite "/d" and "/c" when it starts cmd.exe;
# this tells the MSYS runtime to leave every argument alone.
set(ENV{MSYS2_ARG_CONV_EXCL} "*")
set(ENV{MSBUILDDISABLENODEREUSE} "1")

execute_process(
    COMMAND cmd.exe /d /c "${_bat}" "${_out}" nofail
    INPUT_FILE "${_null}"
    RESULT_VARIABLE _rc
    TIMEOUT 600)

if(NOT _rc EQUAL 0)
    message(WARNING "SilentHillPC_Launcher.exe was not built (${_rc}). The game itself is fine; "
                    "run pc_port\\build_launcher.bat by hand to see why.")
endif()
file(TOUCH "${STAMP}")
