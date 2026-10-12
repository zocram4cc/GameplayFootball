add_test(NAME repo_pii_scan
  COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_pii.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
