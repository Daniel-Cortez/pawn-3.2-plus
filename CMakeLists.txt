# Copyright (c) 2015-2017 Stanislav Gromov.

cmake_minimum_required(VERSION 2.6)
include("${CMAKE_SOURCE_DIR}/cmake/SetBuildFlags.txt" OPTIONAL)
include("CheckCSourceCompiles")
include("CheckIncludeFile")

project(pawn C)


# Initialize variables for compile flags
set(PAWNAMX_COMPILE_FLAGS "")
set(PAWNCC_COMPILE_FLAGS "")
set(SHARED_COMPILE_FLAGS "")
set(SHARED_INCLUDE_PATHS "")

if(WIN32 OR UNIX OR APPLE)
	option(PAWNAMX_BUILD_PAWNCC "Build the Pawn compiler" ON)
endif()
option(PAWNAMX_USE_64BIT_CELLS "Use 64-bit cells (default: 32)" OFF)
if(PAWNAMX_USE_64BIT_CELLS)
	set(PAWNAMX_CELL_SIZE 64)
else()
	set(PAWNAMX_CELL_SIZE 32)
endif()
set(SHARED_COMPILE_FLAGS "${SHARED_COMPILE_FLAGS} -DPAWN_CELL_SIZE=${PAWNAMX_CELL_SIZE}")
option(PAWNAMX_NO_RUNTIME_CHECKS "Disable runtime checks" OFF)
if(PAWNAMX_NO_RUNTIME_CHECKS)
	set(PAWNAMX_COMPILE_FLAGS "${PAWNAMX_COMPILE_FLAGS} -DAMX_NO_RUNTIME_CHECKS")
endif()

# Define "LINUX" compiler flag if it's not defined while "__linux" or "__linux__" flags are present
if(UNIX)
	check_c_source_compiles(
		"
		#if (!defined LINUX) && (defined __linux || defined __linux__)
			#error ...
		#endif
		int main(void) { return 0; }
		"
		SHOULD_NOT_FORCE_LINUX_COMPILE_FLAG
	)
	if(SHOULD_NOT_FORCE_LINUX_COMPILE_FLAG)
		set(SHARED_COMPILE_FLAGS "${SHARED_COMPILE_FLAGS} -DLINUX")
	endif()
endif()

# Only relocate P-Code on supported compilers
set(PAWNAMX_RELOCATE_PCODE FALSE)
foreach(COMPILER_ID "GNU" "Clang" "AppleClang" "Intel")
	if(CMAKE_CC_COMPILER_ID MATCHES COMPILER_ID)
		set(PAWNAMX_RELOCATE_PCODE TRUE)
	endif()
endforeach()
# Also disable relocation if Pawn cell can't fit a pointer
if(PAWNAMX_RELOCATE_PCODE)
	math(EXPR PAWNAMX_POINTER_SIZE "${CMAKE_SIZEOF_VOID_P} * 8")
	if(PAWNAMX_POINTER_SIZE GREATER PAWNAMX_CELL_SIZE)
		set(PAWNAMX_RELOCATE_PCODE FALSE)
	endif()
endif()
if(NOT PAWNAMX_RELOCATE_PCODE)
	set(PAWNAMX_COMPILE_FLAGS "${PAWNAMX_COMPILE_FLAGS} -DAMX_DONT_RELOCATE")
endif()


# Check include files availability
set(REQUIRED_INCLUDE_FILES
	"unistd.h"
	"inttypes.h"
	"stdint.h"
	"malloc.h"
	"tchar.h"
	"curses.h"
	"utime.h"
)
foreach(INCLUDE_FILE ${REQUIRED_INCLUDE_FILES})
	string(REPLACE "." "_" DEFINITION_NAME "HAVE_${INCLUDE_FILE}")
	string(TOUPPER ${DEFINITION_NAME} DEFINITION_NAME)
	check_include_file("${INCLUDE_FILE}" ${DEFINITION_NAME})
	if(${DEFINITION_NAME})
		set(SHARED_COMPILE_FLAGS "${SHARED_COMPILE_FLAGS} -D${DEFINITION_NAME}=1")
	endif()
endforeach()
# As an exception, HAVE_ALLOCA_H must be defined as "0" if alloca.h is not available
# TODO: Fix this in amx.h
check_include_file("alloca.h" HAVE_ALLOCA_H)
if(HAVE_ALLOCA_H)
	set(SHARED_COMPILE_FLAGS "${SHARED_COMPILE_FLAGS} -DHAVE_ALLOCA_H=1")
else()
	set(SHARED_COMPILE_FLAGS "${SHARED_COMPILE_FLAGS} -DHAVE_ALLOCA_H=0")
endif()

# Check whether the target platform has Big Endian or Little Endian byte order
include(TestBigEndian)
test_big_endian(BYTE_ORDER_BIG_ENDIAN)
if(BYTE_ORDER_BIG_ENDIAN)
	set(SHARED_COMPILE_FLAGS "${SHARED_COMPILE_FLAGS} -DBYTE_ORDER=BIG_ENDIAN")
else()
	set(SHARED_COMPILE_FLAGS "${SHARED_COMPILE_FLAGS} -DBYTE_ORDER=LITTLE_ENDIAN")
endif()

# Get rid of some useless warning messages
if(WIN32 AND MSVC)
	add_definitions("/D_CRT_SECURE_NO_WARNINGS")
	add_definitions("/wd4996")
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
	add_definitions("-w")
endif()

# Strip out dynamic libraries loading, "@timer" and "@keypressed" callbacks
set(PAWNAMX_COMPILE_FLAGS "${PAWNAMX_COMPILE_FLAGS} -DAMX_NODYNALOAD -DAMXCONSOLE_NOIDLE -DAMXTIME_NOIDLE")

# Add floating-point numbers support
set(PAWNAMX_COMPILE_FLAGS "${PAWNAMX_COMPILE_FLAGS} -DFLOATPOINT")

# Enable relocation on Unix-like systems.
if(UNIX)
	set(PAWNCC_COMPILE_FLAGS "${PAWNCC_COMPILE_FLAGS} -DENABLE_BINRELOC")
endif()

# Set shared include paths
set(SHARED_INCLUDE_PATHS "${SHARED_INCLUDE_PATHS};${CMAKE_CURRENT_SOURCE_DIR}")
set(SHARED_INCLUDE_PATHS "${SHARED_INCLUDE_PATHS};${CMAKE_CURRENT_SOURCE_DIR}/source/amx")
# Include "pawn/linux" on Unix-like systems
if(UNIX)
	set(SHARED_INCLUDE_PATHS "${SHARED_INCLUDE_PATHS};${CMAKE_CURRENT_SOURCE_DIR}/source/linux")
endif()
include_directories("${SHARED_INCLUDE_PATHS}")


# Add pawnamx static library
set(PAWNAMX_SOURCE_FILES
	"source/amx/amx.h"
	"source/amx/amx_internal.h"
	"source/amx/amx.c"
	"source/amx/amxaux.h"
	"source/amx/amxaux.c"
	"source/amx/amxdbg.h"
	"source/amx/amxdbg.c"
	"source/amx/osdefs.h"
	"source/amx/amxcore.c"
	"source/amx/amxcons.h"
	"source/amx/amxcons.c"
	"source/amx/amxfile.c"
	"source/amx/float.c"
	"source/amx/amxstring.c"
	"source/amx/amxtime.c"
)
if(UNIX)
	set(PAWNAMX_SOURCE_FILES "${PAWNAMX_SOURCE_FILES}"
		"source/linux/binreloc.h"
		"source/linux/binreloc.c"
		"source/linux/getch.h"
		"source/linux/getch.c"
	)
endif()
add_library(pawnamx STATIC ${PAWNAMX_SOURCE_FILES})
if(WIN32)
	target_link_libraries(pawnamx winmm)
elseif(UNIX AND NOT APPLE)
	target_link_libraries(pawnamx m)
endif()
set_target_properties(pawnamx PROPERTIES "COMPILE_FLAGS" "${PAWNAMX_COMPILE_FLAGS} ${SHARED_COMPILE_FLAGS}")


# Add pawncc executable
if(PAWNAMX_BUILD_PAWNCC)
	add_subdirectory(
		"${CMAKE_CURRENT_SOURCE_DIR}/source/compiler"
		"${CMAKE_CURRENT_BINARY_DIR}/compiler"
	)
	set(EXTRA_FLAGS "")
	if(MSVC)
		set(EXTRA_FLAGS "-wd4267 -wd4312 -wd4477")
	endif()
	set_target_properties(pawnc PROPERTIES "COMPILE_FLAGS" "${PAWNCC_COMPILE_FLAGS} ${SHARED_COMPILE_FLAGS} ${EXTRA_FLAGS}")
	set_target_properties(pawndisasm PROPERTIES "COMPILE_FLAGS" "${PAWNAMX_COMPILE_FLAGS} ${SHARED_COMPILE_FLAGS} ${EXTRA_FLAGS}")
endif()


# Export shared definitions
set_target_properties(pawnamx PROPERTIES "SHARED_COMPILE_FLAGS" "${SHARED_COMPILE_FLAGS}")
set_target_properties(pawnamx PROPERTIES "SHARED_INCLUDE_PATHS" "${SHARED_INCLUDE_PATHS}")
