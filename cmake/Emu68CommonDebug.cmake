# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#
# Debug output configuration: one sink, one verbosity tier.  The runtime
# mechanism lives in emu68-common/include/debug.h; this module turns the two
# choices into compile definitions and link steps.
#
# Sink - EMU68_DEBUG_BACKEND.  A stack-wide property (one libcommon.a is shared
# by every component), so it is set once at the top-level build:
#
#   pistorm (default) - magic 0xdeadbeef trap (Emu68/PiStorm).
#   serial            - Exec RawPutChar, the kprintf path: the serial port, or
#                       whatever redirects it (Sashimi and the like).
#   off               - no sink at all; every tier below is forced silent.
#
# Both sinks format with emu68-common's fmt_vformat (no Exec call) and are
# ROM-able; nothing extra is linked for either.  The serial sink reads SysBase
# from address 4 (debug printing has no context to carry it).
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
