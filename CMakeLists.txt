project(pawn C)
cmake_minimum_required(VERSION 2.8.11 FATAL_ERROR)

add_subdirectory(
  "${CMAKE_CURRENT_SOURCE_DIR}/source/amx"
  "${CMAKE_CURRENT_BINARY_DIR}/amx"
)
include("${CMAKE_CURRENT_SOURCE_DIR}/source/cmake/shared-properties.cmake")

option(PAWN_BUILD_PAWNCC "Build Pawn compiler" ON)
if(PAWN_BUILD_PAWNCC)
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/source/compiler"
    "${CMAKE_CURRENT_BINARY_DIR}/compiler"
  )
  target_compile_definitions(pawnc PUBLIC ${SHARED_COMPILE_DEFINITIONS})
  target_compile_definitions(pawndisasm PUBLIC ${SHARED_COMPILE_DEFINITIONS} "-DAMX_ALIGN")
  if(BUILD_TESTING)
    target_compile_definitions(pawnruns PUBLIC ${SHARED_COMPILE_DEFINITIONS})
  endif()
  if(MSVC)
    target_compile_options(pawnc PUBLIC "-wd4267" "-wd4312" "-wd4477")
    target_compile_options(pawndisasm PUBLIC "-wd4267" "-wd4312" "-wd4477")
  endif()
endif()
