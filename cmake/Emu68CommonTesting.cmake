# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#
# emu68_logic_program(<name> SOURCES <file>... [INCLUDES <dir>...] [LIBRARIES <target>...]
#                     [OPTIONS <flag>...])
#
# One logic test program (testing/README.md): a component's own sources built
# as a hosted m68k CLI program for vamos on the host, never installed. It
# links Emu68Common::logic (the fake exec and utility bases, the step loop,
# the kv grammar) and Emu68Common::common.
#
# The exec LVO cache operations are forced: vamos runs no LINE-F opcode. The
# sources must not write to the debug sink either, as nothing answers at the
# PiStorm's debug port under vamos.
function(emu68_logic_program name)
    cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDES;LIBRARIES;OPTIONS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_compile_options(${name} PRIVATE -O2 -Wall -Wextra ${ARG_OPTIONS})
    target_compile_definitions(${name} PRIVATE EMU68_FORCE_LVO_CACHE_OPS)
    target_include_directories(${name} PRIVATE ${ARG_INCLUDES})
    target_link_libraries(${name} PRIVATE Emu68Common::logic Emu68Common::common ${ARG_LIBRARIES})
endfunction()
