project(pawn C)
cmake_minimum_required(VERSION 2.6)
include("${CMAKE_CURRENT_SOURCE_DIR}/source/amx/SharedCompileFlags.cmake")

add_subdirectory(
  "${CMAKE_CURRENT_SOURCE_DIR}/source/amx"
)

option(PAWN_BUILD_PAWNCC "Build the Pawn compiler" ON)
if(PAWN_BUILD_PAWNCC)
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/source/compiler"
  )
  set(EXTRA_FLAGS "")
  if(MSVC)
    set(EXTRA_FLAGS "-wd4267 -wd4312 -wd4477")
  endif()
  set_property(TARGET pawnc pawndisasm APPEND_STRING
    PROPERTY COMPILE_FLAGS "${SHARED_COMPILE_FLAGS} ${EXTRA_FLAGS}")
endif()

