# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#
# Debug output configuration: one sink, one verbosity tier.  The runtime
# mechanism lives in emu68-common/include/debug.h; this module turns the two
# choices into compile definitions and link steps.
#
# Sink - EMU68_DEBUG_BACKEND.  A stack-wide property (one libcommon.a is shared
# by every component), so it is set once at the top-level build:
#
#   pistorm (default) - RawDoFmt -> magic 0xdeadbeef trap (Emu68/PiStorm). ROM-able.
#   serial            - debug.lib KPrintF -> console (serial @ 9600). Links libdebug.a,
#                       which carries a 4-byte writable _SysBase, so NOT ROM-able.
#   off               - no sink at all; every tier below is forced silent.
#
# Tier - EMU68_TIER, resolved per component by the top-level build.  A cumulative
# ladder: each rung defines its own macro plus every rung beneath it.
#
#   off      -                     nothing is emitted
#   profile  - PROFILE             timing probes + perf_report only (debug.h KprintfP)
#   debug    - PROFILE DEBUG       asserts and ordinary logging      (debug.h Kprintf)
#   trace    - PROFILE DEBUG TRACE verbose logging                   (debug.h KprintfT)
#
# DEBUG_SINK is set whenever the backend is not "off", and is deliberately NOT a
# tier macro: it gates the *formatter* (debug.h PrintPistorm, perf.c's reporter)
# while the tier macros gate the *printers*.  That split is what lets a component
# built at tier "off" still link against a libcommon.a whose perf_report() a
# tier-"profile" component calls.

set(EMU68_DEBUG_BACKEND "pistorm" CACHE STRING "Debug output backend: pistorm | serial | off")
set_property(CACHE EMU68_DEBUG_BACKEND PROPERTY STRINGS pistorm serial off)

if(NOT EMU68_DEBUG_BACKEND MATCHES "^(pistorm|serial|off)$")
    message(FATAL_ERROR
        "EMU68_DEBUG_BACKEND must be pistorm, serial or off (got '${EMU68_DEBUG_BACKEND}')")
endif()

# The ladder, ascending.  Single source of truth: the macro a rung emits is its
# own name uppercased, and emu68_tier_at_least() compares positions in this list.
set(EMU68_TIER_LADDER off profile debug trace CACHE INTERNAL "emu68 debug tiers, ascending")

set(EMU68_TIER "debug" CACHE STRING "Debug tier for this component: off | profile | debug | trace")
set_property(CACHE EMU68_TIER PROPERTY STRINGS ${EMU68_TIER_LADDER})

if(NOT EMU68_TIER IN_LIST EMU68_TIER_LADDER)
    message(FATAL_ERROR
        "EMU68_TIER must be one of: ${EMU68_TIER_LADDER} (got '${EMU68_TIER}')")
endif()

# Weak __divsi3 helper that debug.lib's single-object kdebug.o drags in via KGetNum
# (which we never call).  Defined weak so libc's strong copy wins for hosted
# programs, while it is the sole definition for freestanding (-nostdlib) targets.
set(_EMU68_DEBUG_SERIAL_GLUE "${CMAKE_CURRENT_LIST_DIR}/emu68_debug_serial_glue.c"
    CACHE INTERNAL "emu68 serial-debug __divsi3 glue source")

# emu68_tier_at_least(<out> <rung>)
# Set <out> to TRUE when this component's tier reaches <rung>.  Always FALSE when
# the backend is off.  For component CMakeLists that must gate something the
# compile definitions cannot express on their own (e.g. nvme.device wiring the
# mounter submodule's own log switches to our tiers).
function(emu68_tier_at_least out rung)
    if(EMU68_DEBUG_BACKEND STREQUAL "off")
        set(${out} FALSE PARENT_SCOPE)
        return()
    endif()
    list(FIND EMU68_TIER_LADDER "${rung}" _want)
    if(_want LESS 0)
        message(FATAL_ERROR "emu68_tier_at_least: unknown tier '${rung}'")
    endif()
    list(FIND EMU68_TIER_LADDER "${EMU68_TIER}" _have)
    if(_have GREATER_EQUAL _want)
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# emu68_debug_definitions()
# Apply the sink + tier compile definitions to the current directory.  Call
# before the directory's targets are defined.
macro(emu68_debug_definitions)
    if(NOT EMU68_DEBUG_BACKEND STREQUAL "off")
        add_compile_definitions(DEBUG_SINK)
        if(EMU68_DEBUG_BACKEND STREQUAL "serial")
            add_compile_definitions(DEBUG_SERIAL)
        endif()
        # Cumulative: every rung from the first real one up to EMU68_TIER.
        list(FIND EMU68_TIER_LADDER "${EMU68_TIER}" _emu68_top)
        if(_emu68_top GREATER 0)
            foreach(_emu68_i RANGE 1 ${_emu68_top})
                list(GET EMU68_TIER_LADDER ${_emu68_i} _emu68_rung)
                string(TOUPPER "${_emu68_rung}" _emu68_rung)
                add_compile_definitions(${_emu68_rung})
            endforeach()
        endif()
    endif()
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
