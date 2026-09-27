// Android input backend: instead of injecting events itself (apps cannot),
// it hands each batch of MouseActions to NativeEngine.onActions(int[]) in
// Kotlin, where the accessibility service turns them into touch gestures.

#include "android_injector.h"

#include <algorithm>
#include <atomic>
#include <vector>

#include "control/mouse_injector.h"
#include "core/log.h"

namespace mausely {
namespace {

JavaVM* g_vm = nullptr;
jclass g_engineClass = nullptr;
jmethodID g_onActions = nullptr;

std::atomic<int> g_width{1080}, g_height{2400};
std::atomic<int64_t> g_doubleTapUs{300'000};

// The processing thread is a native thread: attach it to the VM on first use
// and detach when it exits.
JNIEnv* threadEnv() {
    struct Attachment {
        JNIEnv* env = nullptr;
        ~Attachment() {
            if (env) g_vm->DetachCurrentThread();
        }
    };
    thread_local Attachment attachment;
    if (attachment.env) return attachment.env;
    JNIEnv* env = nullptr;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) return env;  // a Java thread
    JavaVMAttachArgs args{JNI_VERSION_1_6, "mausely-pipeline", nullptr};
    if (g_vm->AttachCurrentThread(&env, &args) != JNI_OK) return nullptr;
    attachment.env = env;
    return env;
}

}  // namespace

bool initInjector(JavaVM* vm, JNIEnv* env, jclass engineClass) {
    g_vm = vm;
    g_engineClass = static_cast<jclass>(env->NewGlobalRef(engineClass));
    g_onActions = env->GetStaticMethodID(engineClass, "onActions", "([I)V");
    return g_onActions != nullptr;
}

void setScreen(int width, int height, int64_t doubleTapUs) {
    g_width = std::max(1, width);
    g_height = std::max(1, height);
    g_doubleTapUs = doubleTapUs;
}

ScreenRect virtualDesktop() { return {0, 0, g_width.load(), g_height.load()}; }

int64_t doubleClickTimeUs() { return g_doubleTapUs; }

void MouseInjector::apply(const MouseActions& actions) {
    if (actions.empty() || !g_onActions) return;
    // Packed as [type, x, y, amount] per action; see NativeEngine.onActions.
    std::vector<jint> packed;
    packed.reserve(actions.size() * 4);
    for (const MouseAction& a : actions) {
        if (a.type == MouseActionType::LeftDown) left_ = true;
        if (a.type == MouseActionType::LeftUp) left_ = false;
        if (a.type == MouseActionType::RightDown) right_ = true;
        if (a.type == MouseActionType::RightUp) right_ = false;
        packed.insert(packed.end(), {static_cast<jint>(a.type), a.x, a.y, a.amount});
    }
    JNIEnv* env = threadEnv();
    if (!env) {
        logError("cannot attach the pipeline thread to the Java VM");
        return;
    }
    jintArray array = env->NewIntArray(static_cast<jsize>(packed.size()));
    if (!array) return;
    env->SetIntArrayRegion(array, 0, static_cast<jsize>(packed.size()), packed.data());
    env->CallStaticVoidMethod(g_engineClass, g_onActions, array);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    env->DeleteLocalRef(array);
}

void MouseInjector::releaseAll() {
    MouseActions a;
    if (left_) a.push_back({MouseActionType::LeftUp});
    if (right_) a.push_back({MouseActionType::RightUp});
    apply(a);
}

}  // namespace mausely
