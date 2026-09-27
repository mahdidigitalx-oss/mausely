#pragma once

#include <jni.h>

#include <cstdint>

namespace mausely {

// Remembers how to reach NativeEngine.onActions(int[]) (call from JNI_OnLoad).
bool initInjector(JavaVM* vm, JNIEnv* env, jclass engineClass);

// Display size in pixels and the system double-tap timeout, as seen by
// virtualDesktop() and doubleClickTimeUs().
void setScreen(int width, int height, int64_t doubleTapUs);

}  // namespace mausely
