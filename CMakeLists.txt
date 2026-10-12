cmake_minimum_required(VERSION 3.24)
project(GodotFootball LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(GODOTCPP_API_VERSION 4.7 CACHE STRING "")
set(GODOTCPP_TARGET template_debug CACHE STRING "")
add_subdirectory(extern/godot-cpp)

include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_library(pesfmt STATIC
  src/pes/strcode.cpp src/pes/cpk.cpp src/pes/fpk.cpp src/pes/vfs.cpp)
target_include_directories(pesfmt PUBLIC src)

add_executable(pesfmt_dump apps/pesfmt_dump.cpp)
target_link_libraries(pesfmt_dump PRIVATE pesfmt)
set_target_properties(pesfmt_dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/apps)

add_library(godotfootball SHARED src/gdext/register_types.cpp)
target_link_libraries(godotfootball PRIVATE godot-cpp)
set_target_properties(godotfootball PROPERTIES
  PREFIX lib
  OUTPUT_NAME godotfootball
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/godot/bin)

enable_testing()
add_subdirectory(tests)
