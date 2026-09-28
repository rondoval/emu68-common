# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#
# emu68_module_layout(<target>) - link <target> with the shared freestanding-module
# layout script (ldscripts/module.lds).
#
# A .device/.library is not an ordinary executable: LoadSeg() starts execution at offset 0
# of the first hunk, the romtag's RT_ENDSKIP must bound the module, and a ROM module may
# contain no writable data at all.  Those are placement facts, and placement is the
# linker's job -- relying on add_executable() source order plus __attribute__((no_reorder))
# only approximates it, and stops approximating it at all once LTO re-partitions the TUs.
#
# Usage:
#   include(cmake/Emu68CommonModuleLayout.cmake)   # or via find_package(Emu68Common)
#   emu68_module_layout(<target> [WRITABLE])
#
# WRITABLE waives the no-writable-section assertion for a module that is only ever
# LoadSeg'd and never placed in ROM.
#
# The entry check has no escape hatch: every module must start with the four bytes
# 70ff4e75 (`moveq #-1,d0; rts`), so a stub that wants to do anything else -- log a line,
# say -- has to be written so the moveq still comes first.

get_filename_component(_emu68_module_ldscript
	"${CMAKE_CURRENT_LIST_DIR}/../ldscripts/module.lds" ABSOLUTE)
set(_EMU68_MODULE_LDSCRIPT "${_emu68_module_ldscript}"
	CACHE INTERNAL "emu68 freestanding module layout script")
unset(_emu68_module_ldscript)

set(_EMU68_LAYOUT_CHECK_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/Emu68CommonLayoutCheckScript.cmake"
	CACHE INTERNAL "emu68_module_layout entry-stub check")

function(emu68_module_layout target)
	cmake_parse_arguments(ARG "WRITABLE" "" "" ${ARGN})
	if(NOT EXISTS "${_EMU68_MODULE_LDSCRIPT}")
		message(FATAL_ERROR
			"emu68_module_layout(${target}): layout script not found at "
			"${_EMU68_MODULE_LDSCRIPT}")
	endif()
	target_link_options(${target} PRIVATE "-Wl,-T,${_EMU68_MODULE_LDSCRIPT}")
	# HUNK carries no symbol table, so the map is the only way to find anything inside a
	# linked module by name.  emu68_isr_z_check() reads it to locate each interrupt
	# server's .text.isr.<name> section; it is also what to read when comparing sizes.
	target_link_options(${target} PRIVATE "-Wl,-Map=$<TARGET_FILE:${target}>.map")
	if(ARG_WRITABLE)
		target_link_options(${target} PRIVATE "-Wl,--defsym,__emu68_writable_ok=1")
	endif()
	# Relink when the contract changes.
	set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${_EMU68_MODULE_LDSCRIPT}")
	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND ${CMAKE_COMMAND}
			-DBINARY=$<TARGET_FILE:${target}>
			-DEXPECT=70ff4e75                   # moveq #-1,d0 ; rts
			-P ${_EMU68_LAYOUT_CHECK_SCRIPT}
		COMMENT "Layout check: ${target} must start with the do-not-execute stub"
		VERBATIM)
endfunction()
