add_executable(sim_database_test sim_database_test.cpp)
target_link_libraries(sim_database_test PRIVATE gfsim GTest::gtest_main)
gtest_discover_tests(sim_database_test)

add_executable(sim_fixture_test sim_fixture_test.cpp)
target_link_libraries(sim_fixture_test PRIVATE gfsim GTest::gtest_main)
target_compile_definitions(sim_fixture_test PRIVATE GF_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(sim_fixture_test)

add_executable(sim_match_test sim_match_test.cpp)
target_link_libraries(sim_match_test PRIVATE gfsim GTest::gtest_main)
target_compile_definitions(sim_match_test PRIVATE GF_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# Whole matches: far beyond gtest_discover_tests' default 5 s discovery budget per test.
gtest_discover_tests(sim_match_test PROPERTIES TIMEOUT 1800)

add_executable(pes_clock_match_test pes_clock_match_test.cpp)
target_link_libraries(pes_clock_match_test PRIVATE gfsim pesfmt GTest::gtest_main)
target_compile_definitions(pes_clock_match_test PRIVATE GF_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(pes_clock_match_test PROPERTIES TIMEOUT 1800)

add_executable(pes_period_end_test pes_period_end_test.cpp)
target_link_libraries(pes_period_end_test PRIVATE gfsim pesfmt GTest::gtest_main)
target_compile_definitions(pes_period_end_test PRIVATE GF_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(pes_period_end_test PROPERTIES TIMEOUT 1800)

add_executable(pes_keeper_test pes_keeper_test.cpp)
target_link_libraries(pes_keeper_test PRIVATE gfsim pesfmt GTest::gtest_main)
gtest_discover_tests(pes_keeper_test)

add_test(NAME gfsim_run_test
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/gfsim_run_test.sh $<TARGET_FILE:gfsim_run> ${CMAKE_SOURCE_DIR})
set_tests_properties(gfsim_run_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)

add_executable(pes_ball_physics_test pes_ball_physics_test.cpp)
target_link_libraries(pes_ball_physics_test PRIVATE gfsim pesfmt GTest::gtest_main)
gtest_discover_tests(pes_ball_physics_test PROPERTIES TIMEOUT 600)

add_executable(pes_judge_test pes_judge_test.cpp)
target_link_libraries(pes_judge_test PRIVATE gfsim pesfmt GTest::gtest_main)
gtest_discover_tests(pes_judge_test)

add_executable(pes_stoppage_timing_test pes_stoppage_timing_test.cpp)
target_link_libraries(pes_stoppage_timing_test PRIVATE gfsim pesfmt GTest::gtest_main)
target_compile_definitions(pes_stoppage_timing_test PRIVATE GF_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(pes_stoppage_timing_test PROPERTIES TIMEOUT 1800)

add_executable(pes_random_test pes_random_test.cpp)
target_link_libraries(pes_random_test PRIVATE gfsim pesfmt GTest::gtest_main)
gtest_discover_tests(pes_random_test)

add_executable(pes_ball_collision_test pes_ball_collision_test.cpp)
target_link_libraries(pes_ball_collision_test PRIVATE gfsim pesfmt GTest::gtest_main)
gtest_discover_tests(pes_ball_collision_test)

add_executable(pes_carrier_test pes_carrier_test.cpp)
target_link_libraries(pes_carrier_test PRIVATE gfsim pesfmt GTest::gtest_main)
gtest_discover_tests(pes_carrier_test)

add_executable(pes_stamina_test pes_stamina_test.cpp)
target_link_libraries(pes_stamina_test PRIVATE gfsim pesfmt GTest::gtest_main)
gtest_discover_tests(pes_stamina_test)

add_executable(sim_stamina_test sim_stamina_test.cpp)
target_link_libraries(sim_stamina_test PRIVATE gfsim pesfmt GTest::gtest_main)
target_compile_definitions(sim_stamina_test PRIVATE GF_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(sim_stamina_test PROPERTIES TIMEOUT 1800)

add_executable(pes_setpiece_taker_test pes_setpiece_taker_test.cpp)
target_link_libraries(pes_setpiece_taker_test PRIVATE gfsim pesfmt GTest::gtest_main)
gtest_discover_tests(pes_setpiece_taker_test)
