cmake_minimum_required(VERSION 3.24)
project(mausely_android LANGUAGES C CXX)

# The platform-independent C++ core (vision, AI, gesture engine, pipeline) is
# shared with the Windows app; its downloads land in the repository's .deps/.
set(MAUSELY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")
add_subdirectory("${MAUSELY_ROOT}" mausely)
# Tracking must run in real time even in debug APKs (the image loops are ~10x
# slower at -O0); ONNX Runtime itself is always an optimised prebuilt.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(mausely_core PRIVATE -O2)
endif()

# JNI bridge + the Android input backend (control/mouse_injector.h).
add_library(mausely SHARED
  jni_bridge.cpp
  android_injector.cpp)
target_link_libraries(mausely PRIVATE mausely_core jnigraphics log)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(mausely PRIVATE -O2)
endif()
# 16 KB page alignment (required by Google Play for Android 15+).
target_link_options(mausely PRIVATE "-Wl,-z,max-page-size=16384")
