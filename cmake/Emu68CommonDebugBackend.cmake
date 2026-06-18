# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#
# Stack-wide selection of the debug-output backend.  The runtime mechanism lives
# in emu68-common/include/debug.h; this module turns one EMU68_DEBUG_BACKEND
# choice into the right compile definitions and link steps.  Because a single
# libcommon.a is shared by every component, the backend is a stack-wide property:
# set EMU68_DEBUG_BACKEND once at the top-level build and it is propagated to all.
#
#   pistorm (default) - RawDoFmt -> magic 0xdeadbeef trap (Emu68/PiStorm). ROM-able.
#   serial            - debug.lib KPrintF -> console (serial @ 9600). Links libdebug.a,
#                       which carries a 4-byte writable _SysBase, so NOT ROM-able.
#   off               - debug fully compiled out.

set(EMU68_DEBUG_BACKEND "pistorm" CACHE STRING "Debug output backend: pistorm | serial | off")
set_property(CACHE EMU68_DEBUG_BACKEND PROPERTY STRINGS pistorm serial off)

if(NOT EMU68_DEBUG_BACKEND MATCHES "^(pistorm|serial|off)$")
    message(FATAL_ERROR
        "EMU68_DEBUG_BACKEND must be pistorm, serial or off (got '${EMU68_DEBUG_BACKEND}')")
endif()

# Weak __divsi3 helper that debug.lib's single-object kdebug.o drags in via KGetNum
# (which we never call).  Defined weak so libc's strong copy wins for hosted
# programs, while it is the sole definition for freestanding (-nostdlib) targets.
set(_EMU68_DEBUG_SERIAL_GLUE "${CMAKE_CURRENT_LIST_DIR}/emu68_debug_serial_glue.c"
    CACHE INTERNAL "emu68 serial-debug __divsi3 glue source")

# emu68_debug_backend_definitions()
# Apply the backend's compile definitions to the current directory.  Call before
# the directory's targets are defined.
macro(emu68_debug_backend_definitions)
    if(EMU68_DEBUG_BACKEND STREQUAL "pistorm")
        add_compile_definitions(DEBUG)
    elseif(EMU68_DEBUG_BACKEND STREQUAL "serial")
        add_compile_definitions(DEBUG DEBUG_SERIAL)
    endif()
    # "off": no DEBUG define -> debug.h compiles the logging macros out.
endmacro()

# emu68_debug_backend_finalize(<target> [ROMABLE])
# Finalize a linked target for the selected backend.
#   serial : link libdebug.a (KPutChar) and add the weak __divsi3 glue it needs.
#            The ROM check is skipped (libdebug.a carries a writable _SysBase).
#   else   : run the ROM check for ROMABLE targets.
# ROMABLE marks the freestanding .device/.library binaries that must stay ROM-able;
# it gates only the ROM check.  (The glue is added regardless, as a harmless weak
# symbol -- hosted programs override it with libc's.)
function(emu68_debug_backend_finalize target)
    cmake_parse_arguments(ARG "ROMABLE" "" "" ${ARGN})
    if(EMU68_DEBUG_BACKEND STREQUAL "serial")
        # -ldebug (libdebug.a); the bare name "debug" is a reserved
        # target_link_libraries keyword, so pass it as a link flag.
        target_link_libraries(${target} PRIVATE -ldebug)
        target_sources(${target} PRIVATE ${_EMU68_DEBUG_SERIAL_GLUE})
    elseif(ARG_ROMABLE AND COMMAND emu68_rom_check)
        emu68_rom_check(${target})
    endif()
endfunction()
