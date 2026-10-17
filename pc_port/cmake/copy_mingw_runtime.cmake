# Copy the MinGW runtime DLLs a built binary needs (libgcc_s_seh-1.dll,
# libwinpthread-1.dll, libstdc++-6.dll, SDL2.dll, OpenAL, libjpeg, ...) next to
# it, so SilentHillPC.exe starts from Explorer, build.bat run or the launcher
# without C:\msys64\mingw64\bin on PATH.
#
# Walks the import tables with objdump, recursively, and copies only DLLs that
# exist in the toolchain's bin directory; Windows system DLLs never match and
# are left alone. Map DLLs in maps/ are scanned too: Windows resolves a loaded
# DLL's own imports from the exe's directory, so one copy there serves them.
#
#   cmake -DBINARY=<exe> -DSEARCH_DIR=<mingw64/bin> -DOBJDUMP=<objdump> [-DEXTRA_GLOB=<glob>] -P copy_mingw_runtime.cmake

cmake_minimum_required(VERSION 3.16)

if(NOT BINARY OR NOT SEARCH_DIR OR NOT OBJDUMP)
    message(FATAL_ERROR "copy_mingw_runtime: BINARY, SEARCH_DIR and OBJDUMP are required")
endif()

get_filename_component(_dest "${BINARY}" DIRECTORY)

set(_queue "${BINARY}")
if(EXTRA_GLOB)
    file(GLOB _extra "${EXTRA_GLOB}")
    list(APPEND _queue ${_extra})
endif()

set(_seen "")
set(_copied 0)

# Lower-cased index of the toolchain's bin dir, built once: import tables spell
# names in whatever case the import library used.
file(GLOB _avail "${SEARCH_DIR}/*")
set(_availKeys "")
foreach(_a IN LISTS _avail)
    get_filename_component(_an "${_a}" NAME)
    string(TOLOWER "${_an}" _an)
    list(APPEND _availKeys "${_an}")
endforeach()

while(_queue)
    list(GET _queue 0 _bin)
    list(REMOVE_AT _queue 0)

    execute_process(COMMAND "${OBJDUMP}" -p "${_bin}"
                    OUTPUT_VARIABLE _dump ERROR_QUIET RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        continue()
    endif()

    string(REGEX MATCHALL "DLL Name: [^\r\n]+" _names "${_dump}")
    foreach(_line IN LISTS _names)
        string(REGEX REPLACE "^DLL Name: " "" _dll "${_line}")
        string(STRIP "${_dll}" _dll)
        string(TOLOWER "${_dll}" _key)
        if(_key IN_LIST _seen)
            continue()
        endif()
        list(APPEND _seen "${_key}")

        list(FIND _availKeys "${_key}" _idx)
        if(_idx LESS 0)
            continue()
        endif()
        list(GET _avail ${_idx} _src)

        get_filename_component(_name "${_src}" NAME)
        set(_differs 1)
        if(EXISTS "${_dest}/${_name}")
            execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${_src}" "${_dest}/${_name}"
                            RESULT_VARIABLE _differs OUTPUT_QUIET ERROR_QUIET)
        endif()
        if(NOT _differs EQUAL 0)
            file(COPY "${_src}" DESTINATION "${_dest}")
            math(EXPR _copied "${_copied} + 1")
        endif()
        list(APPEND _queue "${_src}")
    endforeach()
endwhile()

if(_copied GREATER 0)
    message(STATUS "copy_mingw_runtime: copied ${_copied} runtime DLL(s) to ${_dest}")
endif()
