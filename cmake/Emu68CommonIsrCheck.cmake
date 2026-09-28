# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
# emu68_isr_z_check(<target> SERVERS <symbol>...) — POST_BUILD guard for Exec
# interrupt servers.
#
# A server tells its caller "this interrupt was mine, stop walking the chain"
# with the Z condition code CLEAR, and "not mine" with Z SET
# (exec.library/AddIntServer — the caller tests the flag, not D0).  m68k GCC
# ends a function that saves exactly one data register with `move.l (sp)+,dN`,
# which sets Z from the RESTORED register instead of from the return value; the
# CCR-transparent `movem.l` it uses for two or more registers does not.  Which
# one you get depends on register allocation, so it changes under an innocent
# edit and fails silently.
#
# This slices each named server out of the LINKED module and fails the build if
# any of its exit paths does not leave Z derived from D0.  The remedy is to write
# that server in assembly — see interrupt-chaining.md in emu68-gic400-library.
#
# It finds the server by SECTION, not by symbol: HUNK carries no symbol table,
# the modules link -s, and most servers are `static`.  Each one declares itself
# with EMU68_INTSERVER(<name>) from <intserver.h>, which puts it in a section of
# its own; the linker map lists that section with its address and size whatever
# the symbol's linkage.  A section attribute survives LTO, so one mechanism
# covers both EMU68_LTO=ON and OFF — and always the bytes that ship, rather than
# a pre-link object that a slim LTO build would not even fill in.
#
# Requires emu68_module_layout(<target>) on the same target: that is what emits
# the -Wl,-Map the check reads.

set(_EMU68_ISR_Z_CHECK_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/../scripts/isr-z-check.py"
    CACHE INTERNAL "emu68_isr_z_check helper script")

function(emu68_isr_z_check target)
    cmake_parse_arguments(ARG "" "" "SERVERS" ${ARGN})
    if(NOT ARG_SERVERS)
        message(FATAL_ERROR "emu68_isr_z_check(${target}): SERVERS is required")
    endif()
    if(NOT EXISTS "${_EMU68_ISR_Z_CHECK_SCRIPT}")
        message(FATAL_ERROR "emu68_isr_z_check: helper script not found at "
                            "${_EMU68_ISR_Z_CHECK_SCRIPT}")
    endif()
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(NOT Python3_Interpreter_FOUND)
        message(FATAL_ERROR "emu68_isr_z_check(${target}): python3 not found")
    endif()
    # Paired components (genet sana2/netdev, xhci context/legacy) share both a
    # project name and a target name, so name the source tree in the log line.
    get_filename_component(_isr_component "${CMAKE_SOURCE_DIR}" NAME)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${_EMU68_ISR_Z_CHECK_SCRIPT}
            --objdump ${CMAKE_OBJDUMP}
            --binary $<TARGET_FILE:${target}>
            --map $<TARGET_FILE:${target}>.map
            ${ARG_SERVERS}
        COMMENT "Interrupt-server Z check: ${_isr_component} ${target} (${ARG_SERVERS})"
        VERBATIM
    )
endfunction()
