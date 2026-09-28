# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
cmake_minimum_required(VERSION 3.14.0)
project(common VERSION 2.0.0 LANGUAGES C ASM)
string(TIMESTAMP CURRENT_DATE "(%d.%m.%Y)")

# Dependencies
find_package(devicetree CONFIG REQUIRED)

# Debug sink + tier selection (shared by the whole stack).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Emu68CommonDebug.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Emu68CommonLto.cmake)
emu68_debug_definitions()

# Set default install prefix to ./install subdirectory
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Install path" FORCE)
endif()

# Source files
file(GLOB_RECURSE common_sources CONFIGURE_DEPENDS src/*.c src/*.S)

# Library target
add_library(common STATIC ${common_sources})

# Freestanding C runtime memory primitives (memset/memcpy/memmove/memcmp) that
# GCC may synthesise at -O3.  Compile their definitions with the loop-idiom and
# builtin recognition disabled so the compiler does not rewrite, e.g., memmove()'s
# own copy loop into a call to memmove() (infinite recursion).
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/memory.c PROPERTIES
	COMPILE_OPTIONS "-fno-tree-loop-distribute-patterns;-fno-builtin")

emu68_enable_lto(common)
# memory.c stays a real object. GCC synthesises memmove/memcmp calls during ltrans
# codegen, after the IR phase has ended.  ld satisfies such a late reference out of
# libcommon.a only when the member is a real object; an IR member can no longer be
# compiled by then and the link ends in `undefined reference to memcmp'.
emu68_lto_keep_real_objects(common ${CMAKE_CURRENT_SOURCE_DIR}/src/memory.c)

# CPU/FPU/frame-pointer flags come from the stack toolchain (cmake/toolchain.cmake,
# knobs M68K_CPU / M68K_FPU) and must not be repeated here: a hardcoded -m68040 is
# emitted after CMAKE_C_FLAGS and would silently override M68K_CPU.
target_compile_options(common
	PRIVATE
		-ffreestanding
		-nostdlib
		-nostartfiles
		-O3
		-Wall
		-Wconversion
		-Wsign-conversion
		-Wshadow
		-Wmissing-prototypes
		-Wstrict-prototypes
		-Wno-unused-function
		-Wno-shift-count-overflow
)

target_include_directories(common
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
)

target_link_libraries(common
	PUBLIC
		devicetree::devicetree
)

add_subdirectory(tools)

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
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Emu68CommonIsrCheck.cmake
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Emu68CommonModuleLayout.cmake
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Emu68CommonLayoutCheckScript.cmake
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Emu68CommonLto.cmake
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Emu68CommonDebug.cmake
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Emu68CommonCacheOps.cmake
	DESTINATION lib/cmake/Emu68Common
)

# emu68_isr_z_check() resolves its helper relative to the cmake module dir
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/scripts/isr-z-check.py
	DESTINATION lib/cmake/scripts
)

# emu68_module_layout() resolves the layout script the same way
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ldscripts/module.lds
	DESTINATION lib/cmake/ldscripts
)
