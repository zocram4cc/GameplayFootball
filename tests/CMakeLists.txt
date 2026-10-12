add_test(NAME repo_pii_scan
  COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_pii.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

include(GoogleTest)
add_executable(pes_strcode_test pes_strcode_test.cpp)
target_link_libraries(pes_strcode_test PRIVATE pesfmt GTest::gtest_main)
gtest_discover_tests(pes_strcode_test)
