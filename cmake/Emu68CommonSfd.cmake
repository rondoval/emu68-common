# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#
# emu68_sfd_headers(<target> OUTPUT_DIR <dir> SFD <file>... [ALL]) - generate the
# clib/inline/pragmas/proto headers of each SFD into <dir> and make <target> the custom
# target that builds them.  Add <dir> to the include path and add_dependencies() on
# <target> wherever the headers are used.
#
# The header base name is the SFD's file name without ".sfd" and without the NDK's
# "_lib" suffix: exec_lib.sfd gives proto/exec.h, Picasso96API_lib.sfd gives
# proto/Picasso96API.h, bcmpcie.sfd gives proto/bcmpcie.h.
#
# ALL builds <target> with the default target (a library that installs the headers);
# without it they are built only when something depends on <target>.
#
# Usage:
#   find_package(Emu68Common CONFIG REQUIRED)    # or include(cmake/Emu68CommonSfd.cmake)
#   emu68_sfd_headers(foo_sfd_headers ALL OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/include
#                     SFD ${CMAKE_CURRENT_SOURCE_DIR}/sfd/foo.sfd)

find_program(SFDC_EXECUTABLE
	NAMES sfdc
	HINTS /opt/m68k-amigaos/bin
)

set(_EMU68_SFD_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/Emu68CommonSfdScript.cmake"
	CACHE INTERNAL "emu68_sfd_headers sfdc runner")

function(emu68_sfd_headers target)
	cmake_parse_arguments(ARG "ALL" "OUTPUT_DIR" "SFD" ${ARGN})
	if(NOT ARG_OUTPUT_DIR OR NOT ARG_SFD)
		message(FATAL_ERROR "emu68_sfd_headers(${target}): OUTPUT_DIR and SFD are required")
	endif()
	if(NOT SFDC_EXECUTABLE)
		message(FATAL_ERROR "emu68_sfd_headers(${target}): sfdc not found")
	endif()

	set(all_headers)
	foreach(sfd IN LISTS ARG_SFD)
		get_filename_component(base "${sfd}" NAME_WE)
		string(REGEX REPLACE "_lib$" "" base "${base}")
		set(headers
			${ARG_OUTPUT_DIR}/clib/${base}_protos.h
			${ARG_OUTPUT_DIR}/inline/${base}.h
			${ARG_OUTPUT_DIR}/pragmas/${base}_pragmas.h
			${ARG_OUTPUT_DIR}/proto/${base}.h
		)
		add_custom_command(
			OUTPUT ${headers}
			COMMAND ${CMAKE_COMMAND}
				-DSFDC_EXECUTABLE=${SFDC_EXECUTABLE}
				-DSFD_FILE=${sfd}
				-DOUTPUT_DIR=${ARG_OUTPUT_DIR}
				-DOUTPUT_BASENAME=${base}
				-P ${_EMU68_SFD_SCRIPT}
			DEPENDS ${sfd} ${_EMU68_SFD_SCRIPT}
			VERBATIM
		)
		list(APPEND all_headers ${headers})
	endforeach()

	if(ARG_ALL)
		add_custom_target(${target} ALL DEPENDS ${all_headers})
	else()
		add_custom_target(${target} DEPENDS ${all_headers})
	endif()
endfunction()
