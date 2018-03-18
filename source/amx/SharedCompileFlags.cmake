# Copyright (c) 2015-2017 Stanislav Gromov.

include("CheckCSourceCompiles")
include("CheckIncludeFile")


set(SHARED_COMPILE_FLAGS "")

if(PAWN_USE_64BIT_CELLS)
  set(PAWNAMX_CELL_SIZE 64)
else()
  set(PAWNAMX_CELL_SIZE 32)
endif()
set(SHARED_COMPILE_FLAGS "${SHARED_COMPILE_FLAGS} -DPAWN_CELL_SIZE=${PAWNAMX_CELL_SIZE}")

# Define "LINUX" compiler flag if it's not defined
# while "__linux" and/or "__linux__" flags are present
if(UNIX)
  check_c_source_compiles(
    "
    #if (!defined LINUX) && (defined __linux || defined __linux__)
      int main(void) { return 0; }
    #else
      #error ...
    #endif
    "
    SHOULD_FORCE_LINUX_COMPILE_FLAG
  )
  if(SHOULD_FORCE_LINUX_COMPILE_FLAG)
    set(SHARED_COMPILE_FLAGS "${SHARED_COMPILE_FLAGS} -DLINUX")
  endif()
endif()

# Check include files availability
set(REQUIRED_INCLUDE_FILES
  "unistd.h"
  "inttypes.h"
  "stdint.h"
  "alloca.h"
)
foreach(INCLUDE_FILE ${REQUIRED_INCLUDE_FILES})
  string(REGEX REPLACE "\\.|/" "_" DEFINITION_NAME "HAVE_${INCLUDE_FILE}")
  string(TOUPPER ${DEFINITION_NAME} DEFINITION_NAME)
  check_include_file("${INCLUDE_FILE}" ${DEFINITION_NAME})
  if(${DEFINITION_NAME})
    set(SHARED_COMPILE_FLAGS "${SHARED_COMPILE_FLAGS} -D${DEFINITION_NAME}")
  endif()
endforeach()

# Check whether the target platform has Big Endian or Little Endian byte order
include(TestBigEndian)
test_big_endian(BYTE_ORDER_BIG_ENDIAN)
if(BYTE_ORDER_BIG_ENDIAN)
  set(SHARED_COMPILE_FLAGS "${SHARED_COMPILE_FLAGS} -DBYTE_ORDER=BIG_ENDIAN")
else()
  set(SHARED_COMPILE_FLAGS "${SHARED_COMPILE_FLAGS} -DBYTE_ORDER=LITTLE_ENDIAN")
endif()
