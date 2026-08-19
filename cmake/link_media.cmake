# Puts the media tree beside the binary without copying it.
#
# The runtime wants media/ next to the executable, and copying it there cost the
# tree 18.6 GB: data/media is 6.2 GB and every build configuration held its own
# byte-identical copy - build/media 7.3 G, build-full 6.3 G, build-asan 2.5 G,
# build-tsan 2.5 G. It also meant every build shovelled gigabytes before it could
# link. A symlink gives the runtime the same path for nothing.
#
# Windows keeps the copy: a symlink there needs either developer mode or elevation,
# and a build that fails on a permission is worse than a build that is fat.
#
#   cmake -DSOURCE=<data/media> -DDEST=<build/media> -P link_media.cmake

if(NOT DEFINED SOURCE OR NOT DEFINED DEST)
  message(FATAL_ERROR "link_media.cmake needs -DSOURCE= and -DDEST=")
endif()

# Windows: copy, because a symlink there needs developer mode or elevation and a
# build that fails on a permission is worse than a build that is fat.
if(FALLBACK_COPY)
  execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory "${SOURCE}" "${DEST}")
  return()
endif()

if(IS_SYMLINK "${DEST}")
  # Already linked. Re-point it only if it aims somewhere else.
  file(READ_SYMLINK "${DEST}" current)
  if(current STREQUAL "${SOURCE}")
    return()
  endif()
  file(REMOVE "${DEST}")
elseif(EXISTS "${DEST}")
  # An earlier build's copy. Reclaiming it is the point.
  message(STATUS "link_media: removing the copied media tree at ${DEST}")
  file(REMOVE_RECURSE "${DEST}")
endif()

file(CREATE_LINK "${SOURCE}" "${DEST}" SYMBOLIC RESULT status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "link_media: could not link ${DEST} -> ${SOURCE}: ${status}")
endif()
