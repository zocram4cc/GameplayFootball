# The GameplayFootball match engine as a headless, deterministic library.
find_package(SQLite3 REQUIRED)
find_package(Boost REQUIRED CONFIG)

file(GLOB_RECURSE GFSIM_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

add_library(gfsim STATIC ${GFSIM_SOURCES})
target_include_directories(gfsim PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gfsim PUBLIC pesrig SQLite3::SQLite3 Boost::headers)
