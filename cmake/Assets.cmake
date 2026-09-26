# Runtime assets: MediaPipe-derived hand models from OpenCV Zoo (Apache-2.0)
# and a few MediaPipe test photos used by the integration tests.

set(OPENCV_ZOO_COMMIT 47534e27c9851bb1128ccc0102f1145e27f23f98)
set(OPENCV_ZOO_URL "https://media.githubusercontent.com/media/opencv/opencv_zoo/${OPENCV_ZOO_COMMIT}/models")

set(MAUSELY_THIRD_PARTY_MODELS
  "palm_detection_mediapipe/palm_detection_mediapipe_2023feb.onnx|78ff51c38496b7fc8b8ebdb6cc8c1abb02fa6c38427c6848254cdaba57fcce7c"
  "handpose_estimation_mediapipe/handpose_estimation_mediapipe_2023feb.onnx|db0898ae717b76b075d9bf563af315b29562e11f8df5027a1ef07b02bef6d81c")

set(MAUSELY_MODEL_FILES "")
foreach(entry IN LISTS MAUSELY_THIRD_PARTY_MODELS)
  string(REPLACE "|" ";" parts "${entry}")
  list(GET parts 0 rel)
  list(GET parts 1 sha)
  get_filename_component(name "${rel}" NAME)
  mausely_download("${OPENCV_ZOO_URL}/${rel}" "${MAUSELY_DEPS_DIR}/models/${name}" ${sha})
  list(APPEND MAUSELY_MODEL_FILES "${MAUSELY_DEPS_DIR}/models/${name}")
endforeach()

# Our own models (trained by training/, committed to the repository).
file(GLOB MAUSELY_OWN_MODELS "${CMAKE_SOURCE_DIR}/models/*.onnx")
list(APPEND MAUSELY_MODEL_FILES ${MAUSELY_OWN_MODELS})

if(MAUSELY_BUILD_TESTS)
  set(MAUSELY_TEST_IMAGES
    "fist.jpg|43fa1cabf3f90d574accc9a56986e2ee48638ce59fc65af1846487f73bb2ef24"
    "pointing_up.jpg|ecf8ca2611d08fa25948a4fc10710af9120e88243a54da6356bacea17ff3e36e"
    "pointing_up_rotated.jpg|50ff66f50281207072a038e5bb6648c43f4aacbfb8204a4d2591868756aaeff1"
    "right_hands.jpg|4b5134daa4cb60465535239535f9f74c2842aba3aa5fd30bf04ef5678f93d87f"
    "left_hands.jpg|240c082e80128ff1ca8a83ce645e2ba4d8bc30f0967b7991cf5fa375bab489e1"
    "thumb_up.jpg|5d673c081ab13b8a1812269ff57047066f9c33c07db5f4178089e8cb3fdc0291"
    "victory.jpg|84cb8853e3df614e0cb5c93a25e3e2f38ea5e4f92fd428ee7d867ed3479d5764")
  foreach(entry IN LISTS MAUSELY_TEST_IMAGES)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 name)
    list(GET parts 1 sha)
    mausely_download("https://storage.googleapis.com/mediapipe-assets/${name}"
      "${MAUSELY_DEPS_DIR}/testimages/${name}" ${sha})
  endforeach()
  set(MAUSELY_TEST_IMAGE_DIR "${MAUSELY_DEPS_DIR}/testimages")
endif()
