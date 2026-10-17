# =============================================================================
# Remix add-on: SilentHillPC_RemixAddon.dll / .so (docs/Remix_Mode.md)
# =============================================================================
# Built like a map overlay DLL: game code and PsyCross resolve from the exe
# (import library on Windows, dlopen-time lookup on ELF/Mach-O), never from a
# private copy. The exe finds it next to itself (pc_remix_host.c); without it
# the game runs vanilla.

file(GLOB SH_REMIX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
add_library(SilentHillPC_RemixAddon SHARED ${SH_REMIX_SOURCES})

target_include_directories(SilentHillPC_RemixAddon PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    $<TARGET_PROPERTY:SilentHillPC,INCLUDE_DIRECTORIES>)
target_compile_definitions(SilentHillPC_RemixAddon PRIVATE
    $<TARGET_PROPERTY:SilentHillPC,COMPILE_DEFINITIONS>
    "static_assert=_Static_assert")
target_compile_options(SilentHillPC_RemixAddon PRIVATE
    -include "${CMAKE_CURRENT_SOURCE_DIR}/include/remix_addon_names.h")
if(NOT MSVC)
    target_compile_options(SilentHillPC_RemixAddon PRIVATE
        -Wno-incompatible-pointer-types
        -Wno-discarded-qualifiers
        -Wno-builtin-declaration-mismatch
        -Wno-int-to-pointer-cast
        -Wno-pointer-to-int-cast
        -Wno-sign-compare
        -Wno-missing-braces
        -Wno-unused-variable
        -Wno-unused-function)
endif()

if(WIN32)
    target_link_libraries(SilentHillPC_RemixAddon PRIVATE SilentHillPC)
    # Same reason as the map DLLs (maps/CMakeLists.txt): CMake's auto-export
    # emits nothing on this toolchain, and the getter must be visible.
    target_link_options(SilentHillPC_RemixAddon PRIVATE "LINKER:--export-all-symbols")
else()
    add_dependencies(SilentHillPC_RemixAddon SilentHillPC)
    if(APPLE)
        target_link_options(SilentHillPC_RemixAddon PRIVATE "-Wl,-undefined,dynamic_lookup")
    else()
        # Bind the add-on's own globals to itself, as the map DLLs do.
        target_link_options(SilentHillPC_RemixAddon PRIVATE "-Wl,-Bsymbolic")
    endif()
endif()

set_target_properties(SilentHillPC_RemixAddon PROPERTIES
    PREFIX ""
    OUTPUT_NAME "SilentHillPC_RemixAddon"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
