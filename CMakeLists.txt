# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
cmake_minimum_required(VERSION 3.14.0)
project(common VERSION 1.6.0 LANGUAGES C ASM)

# Dependencies
find_package(devicetree CONFIG REQUIRED)

# Debug-output backend selection (shared by the whole stack).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Emu68CommonDebugBackend.cmake)
emu68_debug_backend_definitions()

# Set default install prefix to ./install subdirectory
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Install path" FORCE)
endif()

# Detect a non-const-correct (pre-AmigaOS-3.2) NDK: its CopyMem() and friends take
# a non-const source, so otherwise const-correct code trips -Wdiscarded-qualifiers.
# When the probe fails we suppress just that one warning class across the stack (the
# code is const-correct for the target NDK 3.2 — there the probe passes and the
# warning stays fully enabled).  The result is baked into the exported package config
# so every find_package(Emu68Common) consumer inherits the same decision.
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-Werror=discarded-qualifiers")
check_c_source_compiles([=[
#include <proto/exec.h>
void f(void) { const char s[1] = {0}; CopyMem(s, (APTR)0, 1); }
]=] EMU68_NDK_COPYMEM_CONST_CORRECT)
unset(CMAKE_REQUIRED_FLAGS)
if(EMU68_NDK_COPYMEM_CONST_CORRECT)
	set(EMU68_NDK_NONCONST 0)
else()
	set(EMU68_NDK_NONCONST 1)
endif()

# Source files
file(GLOB_RECURSE common_sources CONFIGURE_DEPENDS src/*.c src/*.S)

# Library target
add_library(common STATIC ${common_sources})

# Freestanding C runtime memory primitives (memset/memcpy/memmove/memcmp) that
# GCC may synthesise at -O3.  Compile their definitions with the loop-idiom and
# builtin recognition disabled so the compiler does not rewrite, e.g., memcpy()'s
# own copy loop into a call to memcpy() (infinite recursion).
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/memory.c PROPERTIES
	COMPILE_OPTIONS "-fno-tree-loop-distribute-patterns;-fno-builtin")

target_compile_options(common
	PRIVATE
		-ffreestanding
		-nostdlib
		-nostartfiles
		-O3
		-m68040
		-fomit-frame-pointer
		-Wall
		-Wconversion
		-Wsign-conversion
		-Wshadow
		-Wmissing-prototypes
		-Wstrict-prototypes
		-Wno-unused-function
		-Wno-shift-count-overflow
)

# Suppress the const-discard artifact on a non-const-correct NDK (see probe above).
if(EMU68_NDK_NONCONST)
	target_compile_options(common PRIVATE -Wno-discarded-qualifiers)
endif()

target_include_directories(common
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
)

target_link_libraries(common
	PUBLIC
		devicetree::devicetree
)

# Install targets
install(TARGETS common
	EXPORT Emu68CommonTargets
	ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)

install(EXPORT Emu68CommonTargets
	NAMESPACE Emu68Common::
	FILE Emu68CommonTargets.cmake
	DESTINATION lib/cmake/Emu68Common
)

# CMake package config
include(CMakePackageConfigHelpers)

configure_package_config_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Emu68CommonConfig.cmake.in
	${CMAKE_CURRENT_BINARY_DIR}/Emu68CommonConfig.cmake
	INSTALL_DESTINATION lib/cmake/Emu68Common
)

write_basic_package_version_file(
	${CMAKE_CURRENT_BINARY_DIR}/Emu68CommonConfigVersion.cmake
	VERSION ${PROJECT_VERSION}
	COMPATIBILITY SameMajorVersion
)

install(FILES
	${CMAKE_CURRENT_BINARY_DIR}/Emu68CommonConfig.cmake
	${CMAKE_CURRENT_BINARY_DIR}/Emu68CommonConfigVersion.cmake
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Emu68CommonRomCheck.cmake
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Emu68CommonRomCheckScript.cmake
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Emu68CommonDebugBackend.cmake
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/emu68_debug_serial_glue.c
	DESTINATION lib/cmake/Emu68Common
)
