project(pawn C)
cmake_minimum_required(VERSION 2.6)
include("${CMAKE_CURRENT_SOURCE_DIR}/source/amx/SharedCompileFlags.cmake")

add_subdirectory(
  "${CMAKE_CURRENT_SOURCE_DIR}/source/amx"
  "${CMAKE_CURRENT_BINARY_DIR}/amx"
)

option(PAWN_BUILD_PAWNCC "Build the Pawn compiler" ON)
if(PAWN_BUILD_PAWNCC)
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/source/compiler"
    "${CMAKE_CURRENT_BINARY_DIR}/compiler"
  )
  set_property(TARGET pawnc pawndisasm APPEND_STRING
    PROPERTY COMPILE_FLAGS " ${SHARED_COMPILE_FLAGS}")
  set_property(TARGET pawndisasm APPEND_STRING
    PROPERTY COMPILE_FLAGS " -DAMX_ALIGN")
  if(MSVC)
    set_property(TARGET pawnc pawndisasm APPEND_STRING
      PROPERTY COMPILE_FLAGS " -wd4267 -wd4312 -wd4477")
  endif()
endif()

