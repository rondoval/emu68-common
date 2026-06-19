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

# Source files
file(GLOB_RECURSE common_sources CONFIGURE_DEPENDS src/*.c src/*.S)

# Library target
add_library(common STATIC ${common_sources})

target_compile_options(common
	PRIVATE
		-ffreestanding
		-nostdlib
		-nostartfiles
		-Os
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
