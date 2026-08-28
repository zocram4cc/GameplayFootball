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
  onthepitch/net_geom_test.cpp
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

# Where a skin weight comes from (vertex colours, or the sidecar that can name a
# finger joint) and which joint its number means.
add_executable(gameplayfootball_skinweights_tests
  onthepitch/skin_weights_test.cpp
  onthepitch/joint_order_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/player/humanoid/skinweights.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/player/humanoid/jointorder.cpp
)

target_compile_definitions(gameplayfootball_skinweights_tests PRIVATE
  GF_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden"
)
target_include_directories(gameplayfootball_skinweights_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(gameplayfootball_skinweights_tests PRIVATE
  gf_mathlib
  GTest::gtest_main
)

gtest_discover_tests(gameplayfootball_skinweights_tests DISCOVERY_MODE PRE_TEST)

# Which finger pose a player wears, and where the pose library comes from.
add_executable(gameplayfootball_handpose_tests
  onthepitch/hand_pose_test.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/handposedata.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/player/humanoid/handrig.cpp
  ${PROJECT_SOURCE_DIR}/src/base/utils.cpp
  ${PROJECT_SOURCE_DIR}/src/types/refcounted.cpp
)

target_include_directories(gameplayfootball_handpose_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(gameplayfootball_handpose_tests PRIVATE
  gf_mathlib
  GTest::gtest_main
)

gtest_discover_tests(gameplayfootball_handpose_tests DISCOVERY_MODE PRE_TEST)

# That a .anim may carry finger channels, and a clip without them leaves the
# fingers to the hand rig.
add_executable(gameplayfootball_animclip_tests
  utils/movement_history_test.cpp
  utils/anim_finger_channels_test.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/animation.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/animationextensions/footballanimationextension.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/xmlloader.cpp
  ${PROJECT_SOURCE_DIR}/src/base/utils.cpp
)

target_include_directories(gameplayfootball_animclip_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(gameplayfootball_animclip_tests PRIVATE
  gf_mathlib
  GTest::gtest_main
)

gtest_discover_tests(gameplayfootball_animclip_tests DISCOVERY_MODE PRE_TEST)

# Which grass tile the procedural pitch is generated from
add_executable(gameplayfootball_pitchturf_tests
  onthepitch/pitch_turf_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/pitchturf.cpp
)
target_include_directories(gameplayfootball_pitchturf_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_pitchturf_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_pitchturf_tests DISCOVERY_MODE PRE_TEST)

# How far the camera has to see for a given stadium
add_executable(gameplayfootball_stadiumfar_tests
  onthepitch/stadium_far_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/stadiumfar.cpp
)
target_include_directories(gameplayfootball_stadiumfar_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_stadiumfar_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_stadiumfar_tests DISCOVERY_MODE PRE_TEST)

# A stadium's own sky colours
add_executable(gameplayfootball_stadiumsky_tests
  onthepitch/stadium_sky_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/stadiumsky.cpp
)
target_include_directories(gameplayfootball_stadiumsky_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_stadiumsky_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_stadiumsky_tests DISCOVERY_MODE PRE_TEST)

# Where a stadium's sun is
add_executable(gameplayfootball_scenelighting_tests
  onthepitch/scene_lighting_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/scenelighting.cpp
)
target_include_directories(gameplayfootball_scenelighting_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_scenelighting_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_scenelighting_tests DISCOVERY_MODE PRE_TEST)

# The rigdio .4ccm music-export parser, condition grammar, and selection
# (docs/RIGDIO.md) — pure, no audio device, no match.
add_executable(gameplayfootball_rigdio_tests
  utils/rigdio_test.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/rigdio.cpp
)
target_include_directories(gameplayfootball_rigdio_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_rigdio_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_rigdio_tests DISCOVERY_MODE PRE_TEST)

# Which celebration a scorer performs, and which camera films it
add_executable(gameplayfootball_goalcelebration_tests
  onthepitch/goal_celebration_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/goalcelebration.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/goalsequence.cpp
)
target_include_directories(gameplayfootball_goalcelebration_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_goalcelebration_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_goalcelebration_tests DISCOVERY_MODE PRE_TEST)

# Who is on the pitch during the entrance
add_executable(gameplayfootball_entrancecast_tests
  onthepitch/entrance_cast_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/entrancecast.cpp
)
target_include_directories(gameplayfootball_entrancecast_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_entrancecast_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_entrancecast_tests DISCOVERY_MODE PRE_TEST)

# The replay wipe's timing
add_executable(gameplayfootball_replaywipe_tests
  onthepitch/replay_wipe_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/replaywipe.cpp
)
target_include_directories(gameplayfootball_replaywipe_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_replaywipe_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_replaywipe_tests DISCOVERY_MODE PRE_TEST)

# Whose badge the crowd's stand flags fly
add_executable(gameplayfootball_teamflag_tests
  onthepitch/team_flag_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/teamflag.cpp
)
target_include_directories(gameplayfootball_teamflag_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_teamflag_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_teamflag_tests DISCOVERY_MODE PRE_TEST)

# Which competition's emblem rings the centre circle
add_executable(gameplayfootball_competitionemblem_tests
  onthepitch/competition_emblem_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/competitionemblem.cpp
)
target_include_directories(gameplayfootball_competitionemblem_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_competitionemblem_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_competitionemblem_tests DISCOVERY_MODE PRE_TEST)

# Which pitch art a ground gets painted with
add_executable(gameplayfootball_pitchoverlay_tests
  onthepitch/pitch_overlay_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/pitchoverlay.cpp
)
target_include_directories(gameplayfootball_pitchoverlay_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_pitchoverlay_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_pitchoverlay_tests DISCOVERY_MODE PRE_TEST)

# Keeping an imported shot out of the players it films
add_executable(gameplayfootball_camerastandoff_tests
  onthepitch/camera_standoff_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/camerastandoff.cpp
)
target_include_directories(gameplayfootball_camerastandoff_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_camerastandoff_tests PRIVATE gf_mathlib GTest::gtest_main)
gtest_discover_tests(gameplayfootball_camerastandoff_tests DISCOVERY_MODE PRE_TEST)

# Keeping a pre-match shot's camera and its choreography together
add_executable(gameplayfootball_prematchpair_tests
  onthepitch/prematch_pair_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/prematchshotpair.cpp
)
target_include_directories(gameplayfootball_prematchpair_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_prematchpair_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_prematchpair_tests DISCOVERY_MODE PRE_TEST)

# Putting an imported entrance staging where our pitch is
add_executable(gameplayfootball_staginganchor_tests
  onthepitch/staging_anchor_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/staginganchor.cpp
)
target_include_directories(gameplayfootball_staginganchor_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_staginganchor_tests PRIVATE gf_mathlib GTest::gtest_main)
gtest_discover_tests(gameplayfootball_staginganchor_tests DISCOVERY_MODE PRE_TEST)

# How fast the exposure is allowed to change
add_executable(gameplayfootball_autoexposure_tests
  onthepitch/auto_exposure_test.cpp
  ${PROJECT_SOURCE_DIR}/src/systems/graphics/rendering/autoexposure.cpp
)
target_include_directories(gameplayfootball_autoexposure_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_autoexposure_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_autoexposure_tests DISCOVERY_MODE PRE_TEST)

# Which of PES's colour grading tables a match is graded through
add_executable(gameplayfootball_scenegrade_tests
  onthepitch/scene_grade_test.cpp
  ${PROJECT_SOURCE_DIR}/src/systems/graphics/scenegrade.cpp
)
target_include_directories(gameplayfootball_scenegrade_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_scenegrade_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_scenegrade_tests DISCOVERY_MODE PRE_TEST)

# Which player body loads, and surviving its absence
add_executable(gameplayfootball_playerbody_tests
  onthepitch/player_body_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/playerbody.cpp
)
target_include_directories(gameplayfootball_playerbody_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_playerbody_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_playerbody_tests DISCOVERY_MODE PRE_TEST)

# What the pre-match screen can offer
add_executable(gameplayfootball_prematchchoices_tests
  menu/prematch_choices_test.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/prematchchoices.cpp
)
target_include_directories(gameplayfootball_prematchchoices_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_prematchchoices_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_prematchchoices_tests DISCOVERY_MODE PRE_TEST)

# What the in-play player indicator says
add_executable(gameplayfootball_setpiecelaws_tests
  onthepitch/set_piece_laws_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/setpiecelaws.cpp
)
target_include_directories(gameplayfootball_setpiecelaws_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gameplayfootball_setpiecelaws_tests GTest::gtest_main)
gtest_discover_tests(gameplayfootball_setpiecelaws_tests DISCOVERY_MODE PRE_TEST)

add_executable(gameplayfootball_faceexpression_tests
  onthepitch/face_expression_test.cpp
)
target_include_directories(gameplayfootball_faceexpression_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gameplayfootball_faceexpression_tests GTest::gtest_main)
gtest_discover_tests(gameplayfootball_faceexpression_tests DISCOVERY_MODE PRE_TEST)

add_executable(gameplayfootball_cutscenesequence_tests
  onthepitch/cutscene_sequence_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/cutscenesequence.cpp
)
target_include_directories(gameplayfootball_cutscenesequence_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gameplayfootball_cutscenesequence_tests GTest::gtest_main)
gtest_discover_tests(gameplayfootball_cutscenesequence_tests DISCOVERY_MODE PRE_TEST)

add_executable(gameplayfootball_planmapcard_tests
  menu/plan_map_card_test.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/widgets/planmapcard.cpp
)
target_include_directories(gameplayfootball_planmapcard_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gameplayfootball_planmapcard_tests GTest::gtest_main)
gtest_discover_tests(gameplayfootball_planmapcard_tests DISCOVERY_MODE PRE_TEST)

add_executable(gameplayfootball_hudindicators_tests
  menu/hud_indicators_test.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/ingame/hudindicators.cpp
)
target_include_directories(gameplayfootball_hudindicators_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_hudindicators_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_hudindicators_tests DISCOVERY_MODE PRE_TEST)

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

# Where the many copies of one model stand (the crowd, the 3D turf)
add_executable(gameplayfootball_instancelist_tests
  base/instance_list_test.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/instancelist.cpp
)
target_include_directories(gameplayfootball_instancelist_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_instancelist_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_instancelist_tests DISCOVERY_MODE PRE_TEST)

# The stadium's hanging surfaces - netting, corner flags, banners.
add_executable(gameplayfootball_cloth_tests
  utils/cloth_test.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/cloth.cpp
)
target_include_directories(gameplayfootball_cloth_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_cloth_tests PRIVATE
  gf_mathlib
  GTest::gtest_main
)
gtest_discover_tests(gameplayfootball_cloth_tests DISCOVERY_MODE PRE_TEST)

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

# The standalone viewer's orbit camera.
add_executable(gameplayfootball_viewercamera_tests
  base/viewer_camera_test.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/viewercamera.cpp
)
target_include_directories(gameplayfootball_viewercamera_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_viewercamera_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_viewercamera_tests DISCOVERY_MODE PRE_TEST)

# Model inventory: what a model is made of, for the standalone viewer and for judging
# an import that dropped geometry.
add_executable(gameplayfootball_modelinventory_tests
  base/model_inventory_test.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/modelinventory.cpp
)
target_include_directories(gameplayfootball_modelinventory_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_modelinventory_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(gameplayfootball_modelinventory_tests DISCOVERY_MODE PRE_TEST)

# ASE normal derivation: normals are 45% of an imported stadium's bytes and, over
# 200,000 sampled faces, 100% of them are flat - so a mesh can ship without them.
add_executable(gameplayfootball_asenormals_tests
  base/ase_normals_test.cpp
  ${PROJECT_SOURCE_DIR}/src/loaders/asenormals.cpp
)
target_include_directories(gameplayfootball_asenormals_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gameplayfootball_asenormals_tests PRIVATE
  gf_mathlib
  GTest::gtest_main
)
gtest_discover_tests(gameplayfootball_asenormals_tests DISCOVERY_MODE PRE_TEST)

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

# Pass-failure breakdown: intercept vs out-of-bounds vs bad trap. The counters
# live behind #ifndef NDEBUG in the game, so this target undefines NDEBUG and
# exercises the guarded behaviour that ships compiled-out of release builds.
add_executable(gameplayfootball_pass_failure_tests
  onthepitch/pass_failure_test.cpp
  ${PROJECT_SOURCE_DIR}/src/data/matchdata.cpp
  ${PROJECT_SOURCE_DIR}/src/data/teamdata.cpp
  ${PROJECT_SOURCE_DIR}/src/data/playerdata.cpp
  ${PROJECT_SOURCE_DIR}/src/data/playertraits.cpp
  ${PROJECT_SOURCE_DIR}/src/gamedefines.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/player/humanoid/skinweights.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/matchpressure.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/database.cpp
  ${PROJECT_SOURCE_DIR}/src/utils/xmlloader.cpp
  ${PROJECT_SOURCE_DIR}/src/utils.cpp
  ${PROJECT_SOURCE_DIR}/src/base/properties.cpp
  ${PROJECT_SOURCE_DIR}/src/base/utils.cpp
  ${PROJECT_SOURCE_DIR}/src/base/log.cpp
)

# Release test builds define NDEBUG; the debug-only counters under test would
# compile away entirely, so take it back off for this target only.
target_compile_options(gameplayfootball_pass_failure_tests PRIVATE -UNDEBUG)

target_include_directories(gameplayfootball_pass_failure_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${GF_BOOST_INCLUDE_DIRS}
)

target_link_libraries(gameplayfootball_pass_failure_tests PRIVATE
  gf_mathlib
  GTest::gtest_main
  SQLite::SQLite3
)

gtest_discover_tests(gameplayfootball_pass_failure_tests DISCOVERY_MODE PRE_TEST)

# Pass-error tuning: the multiplier/web-scale bar a drilled short-passing
# style has to meet. Pure logic, no SDL dependency.
add_executable(gameplayfootball_pass_error_tests
  onthepitch/pass_error_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/teamphilosophy.cpp
  ${PROJECT_SOURCE_DIR}/src/base/properties.cpp
  ${PROJECT_SOURCE_DIR}/src/base/utils.cpp
  ${PROJECT_SOURCE_DIR}/src/base/log.cpp
)

target_include_directories(gameplayfootball_pass_error_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${GF_BOOST_INCLUDE_DIRS}
)

target_link_libraries(gameplayfootball_pass_error_tests PRIVATE
  gf_mathlib
  GTest::gtest_main
)

gtest_discover_tests(gameplayfootball_pass_error_tests DISCOVERY_MODE PRE_TEST)

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
  data/team_tactics_test.cpp
  onthepitch/coach_mode_test.cpp
  onthepitch/substitutions_test.cpp
  onthepitch/ai_manager_test.cpp
  onthepitch/match_progression_test.cpp
  onthepitch/goal_sequence_test.cpp
  onthepitch/foul_sequence_test.cpp
  onthepitch/cutscene_viewer_test.cpp
  onthepitch/cutscene_playback_test.cpp
  ${PROJECT_SOURCE_DIR}/src/onthepitch/cutsceneplayback.cpp
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
  menu/fixture_request_test.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/ingame/formationgraphiclayout.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/ingame/bannerpresentation.cpp
  ${PROJECT_SOURCE_DIR}/src/menu/startmatch/fixturerequest.cpp
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
