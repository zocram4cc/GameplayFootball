add_test(NAME repo_pii_scan
  COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_pii.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

include(GoogleTest)
add_executable(pes_strcode_test pes_strcode_test.cpp)
target_link_libraries(pes_strcode_test PRIVATE pesfmt GTest::gtest_main)
gtest_discover_tests(pes_strcode_test)

add_executable(pes_rig_test pes_rig_test.cpp)
target_link_libraries(pes_rig_test PRIVATE pesrig GTest::gtest_main)
gtest_discover_tests(pes_rig_test)

add_executable(pes_formats_test pes_formats_test.cpp)
target_link_libraries(pes_formats_test PRIVATE pesfmt GTest::gtest_main)
gtest_discover_tests(pes_formats_test)

add_executable(pes_anim_test pes_anim_test.cpp)
target_link_libraries(pes_anim_test PRIVATE pesrig GTest::gtest_main)
gtest_discover_tests(pes_anim_test)

add_executable(pes_clip_pose_test pes_clip_pose_test.cpp)
target_link_libraries(pes_clip_pose_test PRIVATE pesrig GTest::gtest_main)
gtest_discover_tests(pes_clip_pose_test)

# Godot-side checks run the real extension headless; 77 = skipped (no PES21_ROOT).
add_test(NAME godot_character_test
  COMMAND ${CMAKE_SOURCE_DIR}/tools/godot.sh --headless --path ${CMAKE_SOURCE_DIR}/godot -s res://tests/test_character.gd)
set_tests_properties(godot_character_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
add_test(NAME godot_player_test
  COMMAND ${CMAKE_SOURCE_DIR}/tools/godot.sh --headless --path ${CMAKE_SOURCE_DIR}/godot -s res://tests/test_player.gd)
set_tests_properties(godot_player_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
add_test(NAME godot_textures_test
  COMMAND ${CMAKE_SOURCE_DIR}/tools/godot.sh --headless --path ${CMAKE_SOURCE_DIR}/godot -s res://tests/test_textures.gd)
set_tests_properties(godot_textures_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
add_test(NAME godot_animation_test
  COMMAND ${CMAKE_SOURCE_DIR}/tools/godot.sh --headless --path ${CMAKE_SOURCE_DIR}/godot res://tests/test_animation.tscn)
# A script error in _ready stops before quit(): the timeout turns a hang into a failure.
set_tests_properties(godot_animation_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
add_subdirectory(sim)

add_executable(pes_uniform_test pes_uniform_test.cpp)
target_link_libraries(pes_uniform_test PRIVATE pesfmt GTest::gtest_main)
gtest_discover_tests(pes_uniform_test)

add_executable(pes_editsave_test pes_editsave_test.cpp)
target_link_libraries(pes_editsave_test PRIVATE pesfmt GTest::gtest_main)
gtest_discover_tests(pes_editsave_test)

add_executable(pes_exe_test pes_exe_test.cpp)
target_link_libraries(pes_exe_test PRIVATE pesfmt GTest::gtest_main)
gtest_discover_tests(pes_exe_test)

add_executable(pes_handseq_test pes_handseq_test.cpp)
target_link_libraries(pes_handseq_test PRIVATE pesfmt GTest::gtest_main)
gtest_discover_tests(pes_handseq_test)

add_executable(pes_marking_test pes_marking_test.cpp)
target_link_libraries(pes_marking_test PRIVATE pesfmt GTest::gtest_main)
gtest_discover_tests(pes_marking_test)
