cmake_minimum_required(VERSION 3.24)
project(mausely VERSION 0.1.0 LANGUAGES C CXX)

# Windows: the full app. Linux/macOS: the platform-independent core and its
# tests (used by CI to keep the core portable). Android: the core only, added
# with add_subdirectory() by android/app/src/main/cpp/CMakeLists.txt.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(ANDROID)
  set(MAUSELY_BUILD_TESTS OFF)
endif()
option(MAUSELY_BUILD_TESTS "Build unit and integration tests" ON)

include(cmake/Dependencies.cmake)
include(cmake/Assets.cmake)

# Common platform / warning settings for our own targets.
add_library(mausely_options INTERFACE)
if(WIN32)
  target_compile_definitions(mausely_options INTERFACE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()
if(MSVC)
  target_compile_options(mausely_options INTERFACE /W4 /permissive- /utf-8)
  target_compile_definitions(mausely_options INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(mausely_options INTERFACE -Wall -Wextra -Wno-missing-field-initializers)
  if(WIN32)
    # Self-contained executables: no libstdc++/libgcc/winpthread DLLs to ship.
    target_link_options(mausely_options INTERFACE -static $<$<CONFIG:Release>:-s>)
  endif()
endif()

# Everything except the window/dashboard and the platform backends: testable
# without a UI and portable to every platform.
add_library(mausely_core STATIC
  src/core/log.cpp
  src/vision/image_ops.cpp
  src/vision/ort_model.cpp
  src/vision/palm_detector.cpp
  src/vision/hand_landmarker.cpp
  src/vision/hand_tracker.cpp
  src/ai/features.cpp
  src/ai/gesture_classifier.cpp
  src/ai/ai_smoother.cpp
  src/ai/smoother.cpp
  src/control/cursor_mapper.cpp
  src/control/gesture_engine.cpp
  src/capture/image_source.cpp
  src/pipeline/settings.cpp
  src/recorder/recorder.cpp)
target_include_directories(mausely_core PUBLIC src)
target_link_libraries(mausely_core PUBLIC mausely_options onnxruntime_headers PRIVATE stb)
if(WIN32)
  target_sources(mausely_core PRIVATE
    src/control/mouse_injector.cpp
    src/capture/mf_camera.cpp
    src/pipeline/pipeline.cpp)
  target_link_libraries(mausely_core PRIVATE mfplat mfreadwrite mf mfuuid ole32 oleaut32 shlwapi shell32 uuid)
else()
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
  target_link_libraries(mausely_core PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()
if(ANDROID)
  # The Android library supplies the input backend (control/mouse_injector.h).
  target_sources(mausely_core PRIVATE src/pipeline/pipeline.cpp)
  target_link_libraries(mausely_core PUBLIC log)
endif()

if(WIN32)
  add_executable(mausely WIN32
    src/app/main.cpp
    src/ui/d3d_window.cpp
    src/ui/dashboard.cpp
    src/app/mausely.rc)
  target_link_libraries(mausely PRIVATE mausely_core imgui implot)
  if(MSVC)
    target_link_options(mausely PRIVATE /MANIFEST:NO)  # the .rc embeds our manifest
  else()
    target_link_options(mausely PRIVATE -municode)     # entry point is wWinMain
  endif()
endif()

if(NOT ANDROID)
  # Ship ONNX Runtime and the models next to the executables. Runs on every
  # build (cheap) so retrained models are always picked up.
  add_custom_target(mausely_assets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_LIBRARY}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${ORT_LIBRARY_NAME}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MAUSELY_MODEL_FILES} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/models"
    VERBATIM)
  if(TARGET mausely)
    add_dependencies(mausely mausely_assets)
  endif()
endif()

if(MAUSELY_BUILD_TESTS)
  enable_testing()
  add_executable(mausely_tests
    tests/test_main.cpp
    tests/test_core.cpp
    tests/test_image_ops.cpp
    tests/test_vision.cpp
    tests/test_features.cpp
    tests/test_smoother.cpp
    tests/test_control.cpp
    tests/test_settings.cpp)
  target_link_libraries(mausely_tests PRIVATE mausely_core doctest stb)
  target_compile_definitions(mausely_tests PRIVATE
    MAUSELY_TEST_IMAGE_DIR="${MAUSELY_TEST_IMAGE_DIR}"
    MAUSELY_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
  add_dependencies(mausely_tests mausely_assets)
  add_test(NAME mausely_tests COMMAND mausely_tests WORKING_DIRECTORY "$<TARGET_FILE_DIR:mausely_tests>")
endif()
