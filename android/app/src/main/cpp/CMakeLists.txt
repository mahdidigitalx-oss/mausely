cmake_minimum_required(VERSION 3.24)
project(mausely_android LANGUAGES C CXX)

# The platform-independent C++ core (vision, AI, gesture engine, pipeline) is
# shared with the Windows app; its downloads land in the repository's .deps/.
set(MAUSELY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")
add_subdirectory("${MAUSELY_ROOT}" mausely)

# JNI bridge + the Android input backend (control/mouse_injector.h).
add_library(mausely SHARED
  jni_bridge.cpp
  android_injector.cpp)
target_link_libraries(mausely PRIVATE mausely_core jnigraphics log)
# 16 KB page alignment (required by Google Play for Android 15+).
target_link_options(mausely PRIVATE "-Wl,-z,max-page-size=16384")
