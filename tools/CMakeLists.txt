# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#
# emu68check — hosted CLI capability probe for the Install script.  A normal
# -mcrt=nix20 program (toolchain default): the freestanding flags on the
# `common` library are PRIVATE to that target and do not reach this directory.
# Debug sink/tier defines are inherited from the parent's
# emu68_debug_definitions() call — do not repeat it here.

add_executable(emu68check
    emu68check.c
)

target_compile_options(emu68check PRIVATE
    -Os
    -Wall
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wmissing-prototypes
    -Wstrict-prototypes
)

target_compile_definitions(emu68check PRIVATE
    VERSTAG="$VER: emu68check ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR} ${CURRENT_DATE}"
)

target_link_options(emu68check PRIVATE -s)

# In-tree target (not the exported Emu68Common::common): brings the include
# dir and devicetree usage requirements, plus _Stricmp from strutil.
target_link_libraries(emu68check PRIVATE common)

# Hosted CLI program: for the serial backend, link debug.lib (KPrintF). libc
# supplies __divsi3, so no glue and no ROM check.
emu68_debug_backend_finalize(emu68check)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/emu68check DESTINATION C)
