# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#
# The stack's common test harness (testing/README.md): what the components'
# test suites share.
#
#   Emu68Common::kv     the output grammar of an Amiga-side test program
#   Emu68Common::logic  support for a logic test program run under vamos: fake
#                       exec and utility bases, the step loop
#   emu68test           the host side, a Python package, installed to
#                       share/emu68-testing/
#
# Hosted (-mcrt=nix20, the toolchain default) code: the freestanding flags of
# `common` are PRIVATE to that target and do not reach this directory.

set(EMU68_TESTING_WARNINGS -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wmissing-prototypes
    -Wstrict-prototypes)

add_library(kv STATIC src/kv.c)
add_library(logic STATIC src/fakelib.c src/fakeexec.c src/steps.c)
foreach(library kv logic)
    target_compile_options(${library} PRIVATE -O2 ${EMU68_TESTING_WARNINGS})
    target_include_directories(${library} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
endforeach()
target_link_libraries(logic PUBLIC kv)

# The names consumers use, for this tree's own programs too.
add_library(Emu68Common::kv ALIAS kv)
add_library(Emu68Common::logic ALIAS logic)
if(NOT TARGET Emu68Common::common)
    add_library(Emu68Common::common ALIAS common)
endif()

# LogicSelf: the harness's own logic program, which the suite in tests/ runs
# under vamos to prove the two libraries. Not installed.
emu68_logic_program(LogicSelf SOURCES selftest/logicself.c OPTIONS ${EMU68_TESTING_WARNINGS})

install(TARGETS kv logic EXPORT Emu68CommonTargets ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY emu68test DESTINATION share/emu68-testing
    FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
install(FILES requirements.txt DESTINATION share/emu68-testing)
