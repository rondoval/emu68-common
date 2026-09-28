# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#
# Link-time optimization, opted in per target.
#
# LTO is deliberately NOT a fourth flag level.  The three documented levels (toolchain /
# tree-wide add_compile_options / per-target -O) stay untouched: this is a CMake target
# PROPERTY, so opting a target in adds no flag anywhere and opting one out is the absence
# of a call.  CMake calls it "interprocedural optimization"; for GCC the property puts
# -flto=auto -fno-fat-lto-objects on BOTH the compile and the link line and switches the
# archive rules to gcc-ar/gcc-ranlib, which is what a static library with IR in it needs.
#
# Only the config-less INTERPROCEDURAL_OPTIMIZATION property is used: the stack never sets
# CMAKE_BUILD_TYPE, so every INTERPROCEDURAL_OPTIMIZATION_<CONFIG> form is inert.  Do not
# add one.
#
#   emu68_enable_lto(<target>)                 opt <target> in
#   emu68_lto_keep_real_objects(<t> <src>...)  keep these TUs real machine code
#
# The toolchain probe runs itself, once per component build, on first use.

set(EMU68_LTO ON CACHE BOOL "Build with GCC link-time optimization")

# check_ipo_supported() is the right probe here for a non-obvious reason: it uses
# try_compile()'s WHOLE-PROJECT form, which does not honour CMAKE_TRY_COMPILE_TARGET_TYPE
# (that governs the source-file form only), so it really does archive through gcc-ar AND
# link through the plugin -- the exact two things a binutils built --disable-plugins cannot
# do.  LANGUAGES C is explicit: CheckIPOSupported ships no ASM test project.
function(emu68_lto_probe)
	if(NOT EMU68_LTO OR DEFINED EMU68_LTO_USABLE)
		return()
	endif()
	include(CheckIPOSupported)
	check_ipo_supported(RESULT _ok OUTPUT _why LANGUAGES C)
	set(EMU68_LTO_USABLE ${_ok} CACHE INTERNAL "LTO usable with this toolchain")
	if(NOT _ok)
		message(WARNING
			"EMU68_LTO=ON but this toolchain cannot do LTO - building WITHOUT it.\n"
			"  ${_why}\n"
			"  The container toolchain (scripts/docker-build.sh) can; a gcc whose binutils\n"
			"  was built --disable-plugins (the /opt/m68k-amigaos native fallback) cannot:\n"
			"  there, any LTO object inside a .a becomes an undefined reference.")
	endif()
endfunction()

function(emu68_enable_lto target)
	emu68_lto_probe()
	if(EMU68_LTO_USABLE)
		set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
		# GCC privatises symbols during LTO and stamps visibility on them; HUNK has no
		# such concept, so the m68k-amigaos backend warns "visibility attribute not
		# supported in this configuration; ignored" once per function at ltrans -- 400+
		# lines with no codegen effect, and nothing in this tree uses visibility.  Scoped
		# to LTO'd targets so a non-LTO build stays strict.
		#
		# What it costs: a MISSPELLED attribute is silenced too -- __attribute__((usedd))
		# or ((sectoin(...))) becomes a no-op without a word.  Both are load-bearing here,
		# which is why emu68_module_layout() asserts the linked module still starts with
		# the do-not-execute stub: verified that a typo'd section(".text.entry") compiles
		# silently under this flag and the layout check still fails the build.
		target_compile_options(${target} PRIVATE -Wno-attributes)
		target_link_options(${target} PRIVATE -Wno-attributes)
	endif()
endfunction()

# emu68_lto_keep_real_objects(<target> <source>...)
# Compile these TUs to real machine code even under LTO.  Two reasons occur here:
#   * a TU whose payload is file-scope asm() has nothing for the IR to carry, and which
#     partition it would land in is unspecified.
#   * emu68-common's memory.c: GCC synthesises memmove/memcmp calls at ltrans, after the
#     IR phase, and ld can only satisfy such a late reference from a REAL archive member
#     -- see its CMakeLists.
#
# An interrupt server is NOT a reason: emu68_isr_z_check() reads the linked module, by
# the section EMU68_INTSERVER() gives each server, so it works with or without LTO.
function(emu68_lto_keep_real_objects target)
	emu68_lto_probe()
	if(EMU68_LTO_USABLE)
		foreach(_s IN LISTS ARGN)
			set_property(SOURCE ${_s} APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
		endforeach()
	endif()
endfunction()
