# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#
# emu68check — hosted CLI capability probe for the Install script.
# memcheck — proves memmove() on m68k, on an Amiga or under vamos; not installed.
# prefscheck — the same for prefs.h and strfit(); not installed.
# Normal -mcrt=nix20 programs (toolchain default): the freestanding flags on the
# `common` library are PRIVATE to that target and do not reach this directory.
# Debug sink/tier defines are inherited from the parent's
# emu68_debug_definitions() call — do not repeat it here.

foreach(tool emu68check memcheck prefscheck)
    add_executable(${tool}
        ${tool}.c
    )

    target_compile_options(${tool} PRIVATE
        -Os
        -Wall
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wmissing-prototypes
        -Wstrict-prototypes
    )

    target_compile_definitions(${tool} PRIVATE
        VERSTAG="$VER: ${tool} ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR} ${CURRENT_DATE}"
    )

    target_link_options(${tool} PRIVATE -s)

    # In-tree target (not the exported Emu68Common::common): brings the include
    # dir, plus _Stricmp from strutil, and the memory primitives.
    target_link_libraries(${tool} PRIVATE common)
endforeach()

# Hosted CLI programs: not ROMABLE, so no ROM check.

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/emu68check DESTINATION C)
