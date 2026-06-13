# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#
# emu68_rom_check(<target>) — POST_BUILD guard for ROM-able binaries:
# fails the build if the linked output contains a non-empty .data or .bss
# section.  ROM modules must keep all mutable state in allocated memory
# (device base, controller structs); a writable static silently breaks
# that.

set(_EMU68_ROM_CHECK_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/Emu68CommonRomCheckScript.cmake"
    CACHE INTERNAL "emu68_rom_check helper script")

function(emu68_rom_check target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DOBJDUMP=${CMAKE_OBJDUMP}
            -DBINARY=$<TARGET_FILE:${target}>
            -P ${_EMU68_ROM_CHECK_SCRIPT}
        COMMENT "ROM check: ${target} must contain no writable data sections"
        VERBATIM
    )
endfunction()
