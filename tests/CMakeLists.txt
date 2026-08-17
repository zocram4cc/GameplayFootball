include(FetchContent)
include(GoogleTest)

find_package(SQLite3 REQUIRED)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

set(GOOGLETEST_FETCHCONTENT_ARGS
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

# DOWNLOAD_EXTRACT_TIMESTAMP was added in CMake 3.24. Ubuntu 22.04 ships
# CMake 3.22, where the unknown keyword is interpreted as a second URL and
# makes FetchContent configuration fail.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
  list(APPEND GOOGLETEST_FETCHCONTENT_ARGS DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()

FetchContent_Declare(googletest ${GOOGLETEST_FETCHCONTENT_ARGS})

FetchContent_MakeAvailable(googletest)

add_executable(gameplayfootball_math_tests
  base/math_test.cpp
  base/skinning_math_test.cpp
  onthepitch/ball_physics_test.cpp
)

target_link_libraries(gameplayfootball_math_tests PRIVATE
  gf_ballphysicslib
  gf_mathlib
  GTest::gtest_main
)

gtest_discover_tests(gameplayfootball_math_tests DISCOVERY_MODE PRE_TEST)

# Skinning-as-affine-transform tests: the fast per-vertex path has to agree with
# the engine's per-influence quaternion arithmetic, or players deform wrongly.
add_executable(gameplayfootball_skinning_tests
  onthepitch/skinning_transform_test.cpp
  onthepitch/skinning_batch_test.cpp
  onthepitch/skinning_rate_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/player/humanoid/skinning.cpp
)

target_link_libraries(gameplayfootball_skinning_tests PRIVATE
  gf_mathlib
  GTest::gtest_main
)

gtest_discover_tests(gameplayfootball_skinning_tests DISCOVERY_MODE PRE_TEST)

# Velocity state machine unit tests (task 2.5)
add_executable(gameplayfootball_velocity_tests
  onthepitch/velocity_state_test.cpp
)

target_include_directories(gameplayfootball_velocity_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${GF_BOOST_INCLUDE_DIRS}
)

target_link_libraries(gameplayfootball_velocity_tests PRIVATE
  GTest::gtest_main
)

gtest_discover_tests(gameplayfootball_velocity_tests DISCOVERY_MODE PRE_TEST)

# Ogg Vorbis decoder unit tests (pes21 import pack audio)
add_executable(gameplayfootball_ogg_tests
  base/oggdecoder_test.cpp
  ${PROJECT_SOURCE_DIR}/src/loaders/oggdecoder.cpp
)

# Player model map unit tests (per-player imported models)
add_executable(gameplayfootball_playermodelmap_tests
  base/playermodelmap_test.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/playermodelmap.cpp
)

# .faceanim loader unit tests (imported PES facial expressions)
add_executable(gameplayfootball_faceanim_tests
  base/faceanim_test.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/faceanim.cpp
)

# Camera track loader unit tests (imported PES camerawork)
add_executable(gameplayfootball_camtrack_tests
  base/camtrack_test.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/camtrack.cpp
)
target_include_directories(gameplayfootball_camtrack_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_camtrack_tests PRIVATE
  GTest::gtest_main
)
gtest_discover_tests(gameplayfootball_camtrack_tests DISCOVERY_MODE PRE_TEST)

# Entrance choreography loader unit tests (imported PES _pl packs)
add_executable(gameplayfootball_entrancechoreo_tests
  base/entrancechoreo_test.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/entrancechoreo.cpp
)
target_include_directories(gameplayfootball_entrancechoreo_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_entrancechoreo_tests PRIVATE
  gf_mathlib
  GTest::gtest_main
)
gtest_discover_tests(gameplayfootball_entrancechoreo_tests DISCOVERY_MODE PRE_TEST)

# Glyph-row downscaling (why the match clock read 65:00 for 45:00)
add_executable(gameplayfootball_surfacescale_tests
  base/surfacescale_test.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/gui2/surfacescale.cpp
)
target_include_directories(gameplayfootball_surfacescale_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_surfacescale_tests PRIVATE
  GTest::gtest_main
)
gtest_discover_tests(gameplayfootball_surfacescale_tests DISCOVERY_MODE PRE_TEST)

# Model viewer bench maths (debug tooling, engine-free)
add_executable(gameplayfootball_modelviewer_tests
  onthepitch/modelviewer_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/modelviewer.cpp
)
target_include_directories(gameplayfootball_modelviewer_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_modelviewer_tests PRIVATE
  gf_mathlib
  GTest::gtest_main
)
gtest_discover_tests(gameplayfootball_modelviewer_tests DISCOVERY_MODE PRE_TEST)

# Replay/cutscene wipe overlay manifest unit tests
add_executable(gameplayfootball_wipeoverlay_tests
  base/wipeoverlay_test.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/wipeoverlay.cpp
)
target_include_directories(gameplayfootball_wipeoverlay_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_wipeoverlay_tests PRIVATE
  GTest::gtest_main
)
gtest_discover_tests(gameplayfootball_wipeoverlay_tests DISCOVERY_MODE PRE_TEST)

# FaceRig data/blend unit tests
add_executable(gameplayfootball_facerig_tests
  base/facerig_test.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/facerigdata.cpp
)
target_include_directories(gameplayfootball_facerig_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_facerig_tests PRIVATE
  GTest::gtest_main
)
gtest_discover_tests(gameplayfootball_facerig_tests DISCOVERY_MODE PRE_TEST)
target_include_directories(gameplayfootball_faceanim_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_faceanim_tests PRIVATE
  GTest::gtest_main
)
gtest_discover_tests(gameplayfootball_faceanim_tests DISCOVERY_MODE PRE_TEST)
target_include_directories(gameplayfootball_playermodelmap_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_playermodelmap_tests PRIVATE
  GTest::gtest_main
)
gtest_discover_tests(gameplayfootball_playermodelmap_tests DISCOVERY_MODE PRE_TEST)

target_include_directories(gameplayfootball_ogg_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(gameplayfootball_ogg_tests PRIVATE
  GTest::gtest_main
)

add_test(NAME oggdecoder COMMAND gameplayfootball_ogg_tests)
set_tests_properties(oggdecoder PROPERTIES
  ENVIRONMENT "GF_TEST_DATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/data")

# Match simulation integration tests (task 2.6)
add_executable(gameplayfootball_integration_tests
  integration/match_simulation_test.cpp
)

target_include_directories(gameplayfootball_integration_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${GF_BOOST_INCLUDE_DIRS}
)

target_link_libraries(gameplayfootball_integration_tests PRIVATE
  GTest::gtest_main
)

gtest_discover_tests(gameplayfootball_integration_tests DISCOVERY_MODE PRE_TEST)

add_executable(gameplayfootball_league_integration_tests
  integration/league_bootstrap_test.cpp
  ${PROJECT_SOURCE_DIR}/src/data/formations.cpp
  ${PROJECT_SOURCE_DIR}/src/league/leaguecode.cpp
  ${PROJECT_SOURCE_DIR}/src/league/leaguesetup.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/database.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/xmlloader.cpp
  ${PROJECT_SOURCE_DIR}/src/base/properties.cpp
  ${PROJECT_SOURCE_DIR}/src/base/utils.cpp
)

target_include_directories(gameplayfootball_league_integration_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(gameplayfootball_league_integration_tests PRIVATE
  gf_mathlib
  GTest::gtest_main
  SQLite::SQLite3
)

gtest_discover_tests(gameplayfootball_league_integration_tests DISCOVERY_MODE PRE_TEST)

# Simulation-depth unit tests (SIMULATION_IMPROVEMENT_PROPOSAL.md / TECHNICAL_ROADMAP.md).
# These cover pure-logic simulation modules, so they build without SDL/OpenGL.
add_executable(gameplayfootball_simulation_tests
  onthepitch/team_philosophy_test.cpp
  onthepitch/team_instructions_test.cpp
  onthepitch/gameplay_tuning_test.cpp
  onthepitch/match_mentality_test.cpp
  data/player_traits_test.cpp
  data/player_cards_test.cpp
  onthepitch/match_pressure_test.cpp
  onthepitch/pitch_conditions_test.cpp
  onthepitch/referee_profile_test.cpp
  onthepitch/offside_rule_test.cpp
  onthepitch/card_book_test.cpp
  onthepitch/foul_severity_test.cpp
  onthepitch/penalty_shootout_test.cpp
  onthepitch/crowd_mood_test.cpp
  data/match_analytics_test.cpp
  data/formations_test.cpp
  onthepitch/coach_mode_test.cpp
  onthepitch/substitutions_test.cpp
  onthepitch/ai_manager_test.cpp
  onthepitch/match_progression_test.cpp
  onthepitch/goal_sequence_test.cpp
  onthepitch/foul_sequence_test.cpp
  onthepitch/cutscene_viewer_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/cutsceneviewer.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/goalsequence.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/foulsequence.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/teamphilosophy.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/teaminstructions.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/matchmentality.cpp
  ${PROJECT_SOURCE_DIR}/src/data/playertraits.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/matchpressure.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/pitchconditions.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/refereeprofile.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/offsiderule.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/cardbook.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/foulseverity.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/penaltyshootout.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/crowdmood.cpp
  ${PROJECT_SOURCE_DIR}/src/data/matchanalytics.cpp
  ${PROJECT_SOURCE_DIR}/src/data/formations.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/coachmode.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/substitutions.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/aimanager.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/matchprogression.cpp
  ${PROJECT_SOURCE_DIR}/src/base/properties.cpp
  ${PROJECT_SOURCE_DIR}/src/base/utils.cpp
  ${PROJECT_SOURCE_DIR}/src/base/log.cpp
)

target_include_directories(gameplayfootball_simulation_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${GF_BOOST_INCLUDE_DIRS}
)

target_link_libraries(gameplayfootball_simulation_tests PRIVATE
  gf_ballphysicslib
  gf_mathlib
  GTest::gtest_main
)

gtest_discover_tests(gameplayfootball_simulation_tests DISCOVERY_MODE PRE_TEST)

# Pre-match formation graphic + in-match banner presentation logic (broadcast
# presentation UI: docs/PRESENTATION_SPEC.md sections 1.1 and 4). Pure logic,
# no TeamData/Gui2/SDL dependency.
add_executable(gameplayfootball_presentation_tests
  menu/formation_graphic_layout_test.cpp
  menu/banner_presentation_test.cpp
  menu/prematch_timeline_test.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/ingame/formationgraphiclayout.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/ingame/bannerpresentation.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/prematchtimeline.cpp
)

target_include_directories(gameplayfootball_presentation_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(gameplayfootball_presentation_tests PRIVATE
  gf_mathlib
  GTest::gtest_main
)

gtest_discover_tests(gameplayfootball_presentation_tests DISCOVERY_MODE PRE_TEST)

# Career data unit tests (Phase 6: tasks 6.13, 6.16, 6.17)
# Also covers the stat-based match simulation (career_database.cpp).
add_executable(gameplayfootball_career_tests
  career/career_modules_test.cpp
  career/careerdata_test.cpp
  career/career_simulation_test.cpp
  career/career_saveload_test.cpp
  career/career_longrun_audit_test.cpp
  ${PROJECT_SOURCE_DIR}/src/data/careerdata.cpp
  ${PROJECT_SOURCE_DIR}/src/data/draftdata.cpp
  ${PROJECT_SOURCE_DIR}/src/data/scoutingdata.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/career/career_board.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/career/career_common.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/career/career_database.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/career/career_finance.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/career/career_persistence.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/career/career_sim.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/career/career_sponsors.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/career/career_staff.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/career/career_training.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/career/career_transfers.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/localization.cpp
  ${PROJECT_SOURCE_DIR}/src/base/log.cpp
)

target_include_directories(gameplayfootball_career_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${GF_BOOST_INCLUDE_DIRS}
  ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(gameplayfootball_career_tests PRIVATE
  GTest::gtest_main
  SQLite::SQLite3
)

gtest_discover_tests(gameplayfootball_career_tests
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  DISCOVERY_MODE PRE_TEST
)
