# Called from C++ (jni_bridge.cpp / android_injector.cpp) by name.
-keep class io.github.mahdidigitalx_oss.mausely.NativeEngine {
    native <methods>;
    public static void onActions(int[]);
}
