// JNI bridge between the Kotlin app (NativeEngine.kt) and the shared C++
// pipeline. Camera frames come in through submitFrame(); mouse actions go out
// through the Android input backend (android_injector.cpp).

#include <android/bitmap.h>
#include <jni.h>

#include <cmath>
#include <mutex>
#include <string>
#include <vector>

#include "android_injector.h"
#include "core/clock.h"
#include "core/log.h"
#include "pipeline/pipeline.h"
#include "pipeline/settings.h"
#include "vision/image_ops.h"
#include "vision/ort_model.h"

using namespace mausely;

namespace {

// Layout of the array returned by snapshot(); mirrored in Snapshot.kt.
constexpr int kFrameWidth = 0, kFrameHeight = 1, kNewFrame = 2, kHandValid = 3, kHandTracked = 4, kPresence = 5,
              kHandedness = 6, kRoi = 7 /* cx, cy, size, angle */, kLandmarks = 11 /* 21 x (x, y) */,
              kProbs = 53, kPose = 58, kEnabled = 59, kFrozen = 60, kFistProgress = 61,
              kCounters = 62 /* left, double, right, drags, toggles, scroll units */, kDropped = 68,
              kClassifierReady = 69, kAiSmootherReady = 70, kRecording = 71, kRecordingWaiting = 72,
              kRecordingSecondsLeft = 73, kRecordedSamples = 74, kMetricCount = 75, kHeaderSize = 76;
// One block per processed frame after the header.
constexpr int kMArrivalMs = 0, kMQueue = 1, kMPalm = 2, kMLandmark = 3, kMClassify = 4, kMSmooth = 5, kMControl = 6,
              kMTotal = 7, kMLatency = 8, kMRanPalm = 9, kMHand = 10, kMSkipped = 11, kMRawCursor = 12,
              kMSmoothCursor = 14, kMRawPoint = 16, kMSmoothPoint = 18, kMetricSize = 20;
static_assert(kLandmarks + 2 * kNumLandmarks == kProbs && kProbs + kNumPoses == kPose);

struct Engine {
    std::mutex mutex;  // start / stop / submitFrame / settings
    Pipeline pipeline;
    Settings settings;
    bool running = false;
    Frame scratch;     // camera thread's conversion buffer (recycled by the pipeline)
    uint64_t frameIndex = 0;
    Snapshot snap;     // UI thread only
};

Engine& engine() {
    static Engine e;
    return e;
}

std::string str(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r(c ? c : "");
    if (c) env->ReleaseStringUTFChars(s, c);
    return r;
}

jstring jstr(JNIEnv* env, const std::string& s) { return env->NewStringUTF(s.c_str()); }

jstring nativeInit(JNIEnv* env, jobject, jstring logFile) {
    logInit(toWide(str(env, logFile)));
    std::string err;
    // A bare name: the library comes from the app's own lib folder (onnxruntime-android AAR).
    if (!OrtRuntime::instance().init(kOrtLibraryName, &err)) {
        logError(err);
        return jstr(env, err);
    }
    return nullptr;
}

jstring defaultSettings(JNIEnv* env, jobject) { return jstr(env, formatSettings(Settings{})); }

jstring normalizeSettings(JNIEnv* env, jobject, jstring text) {
    Settings s;
    parseSettings(str(env, text), s);
    return jstr(env, formatSettings(s));
}

jstring start(JNIEnv* env, jobject, jstring modelDir, jstring settingsText) {
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex);
    e.pipeline.stop();
    e.running = false;
    e.settings = Settings{};
    parseSettings(str(env, settingsText), e.settings);
    std::string err;
    if (!e.pipeline.start(e.settings, toWide(str(env, modelDir)), nullptr, &err)) {
        e.pipeline.stop();  // without hand tracking there is nothing to control
        return jstr(env, err);
    }
    e.running = true;
    e.frameIndex = 0;
    logInfo("pipeline started");
    return nullptr;
}

void stop(JNIEnv*, jobject) {
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex);
    e.pipeline.stop();
    e.running = false;
}

void submitFrame(JNIEnv* env, jobject, jobject buffer, jint width, jint height, jint rowStride, jint rotation) {
    Engine& e = engine();
    const auto* rgba = static_cast<const uint8_t*>(env->GetDirectBufferAddress(buffer));
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (!rgba || width <= 0 || height <= 0 || rowStride < width * 4 ||
        capacity < static_cast<jlong>(rowStride) * (height - 1) + width * 4)
        return;
    std::lock_guard<std::mutex> lock(e.mutex);
    if (!e.running) return;
    rgbaToFrame(rgba, width, height, rowStride, rotation, e.scratch);
    e.scratch.arrivalUs = Clock::nowUs();
    e.scratch.index = e.frameIndex++;
    e.pipeline.submitFrame(e.scratch);
}

void updateSettings(JNIEnv* env, jobject, jstring text) {
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex);
    e.settings = Settings{};
    parseSettings(str(env, text), e.settings);
    if (e.running) e.pipeline.updateSettings(e.settings);
}

void nativeSetScreen(JNIEnv*, jobject, jint width, jint height, jint doubleTapMs) {
    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex);
    setScreen(width, height, static_cast<int64_t>(doubleTapMs) * 1000);
    if (e.running) e.pipeline.updateSettings(e.settings);  // re-maps the active region onto the new size
}

void setEnabled(JNIEnv*, jobject, jboolean on) { engine().pipeline.setEnabled(on); }

void toggleEnabled(JNIEnv*, jobject) { engine().pipeline.toggleEnabled(); }

void status(JNIEnv* env, jobject, jintArray out) {
    const LiveStatus s = engine().pipeline.status();
    const jint v[5] = {s.enabled, s.hand, s.frozen, static_cast<jint>(s.pose),
                       static_cast<jint>(std::lround(s.fistProgress * 1000.f))};
    if (env->GetArrayLength(out) >= 5) env->SetIntArrayRegion(out, 0, 5, v);
}

jdoubleArray snapshot(JNIEnv* env, jobject) {
    Snapshot& s = engine().snap;
    engine().pipeline.snapshot(s);
    std::vector<double> v(kHeaderSize + s.metrics.size() * kMetricSize, 0.0);
    v[kFrameWidth] = s.frame.width;
    v[kFrameHeight] = s.frame.height;
    v[kNewFrame] = s.newFrame;
    v[kHandValid] = s.hand.valid;
    v[kHandTracked] = s.hand.tracked;
    v[kPresence] = s.hand.lm.presence;
    v[kHandedness] = s.hand.lm.handedness;
    v[kRoi] = s.hand.roi.cx;
    v[kRoi + 1] = s.hand.roi.cy;
    v[kRoi + 2] = s.hand.roi.size;
    v[kRoi + 3] = s.hand.roi.angle;
    for (int i = 0; i < kNumLandmarks; ++i) {
        v[kLandmarks + i * 2] = s.hand.lm.image[i].x;
        v[kLandmarks + i * 2 + 1] = s.hand.lm.image[i].y;
    }
    for (int i = 0; i < kNumPoses; ++i) v[kProbs + i] = s.probs[i];
    v[kPose] = static_cast<int>(s.pose);
    v[kEnabled] = s.enabled;
    v[kFrozen] = s.frozen;
    v[kFistProgress] = s.fistProgress;
    const GestureCounters& c = s.counters;
    const double counters[] = {static_cast<double>(c.leftClicks), static_cast<double>(c.doubleClicks),
                               static_cast<double>(c.rightClicks), static_cast<double>(c.drags),
                               static_cast<double>(c.toggles), static_cast<double>(c.scrollUnits)};
    for (int i = 0; i < 6; ++i) v[kCounters + i] = counters[i];
    v[kDropped] = static_cast<double>(s.droppedFrames);
    v[kClassifierReady] = s.classifierReady;
    v[kAiSmootherReady] = s.aiSmootherReady;
    v[kRecording] = s.recording;
    v[kRecordingWaiting] = s.recordingWaiting;
    v[kRecordingSecondsLeft] = s.recordingSecondsLeft;
    v[kRecordedSamples] = s.recordedSamples;
    v[kMetricCount] = static_cast<double>(s.metrics.size());
    for (size_t i = 0; i < s.metrics.size(); ++i) {
        const FrameMetrics& m = s.metrics[i];
        double* o = v.data() + kHeaderSize + i * kMetricSize;
        o[kMArrivalMs] = static_cast<double>(m.arrivalUs) / 1000.0;
        o[kMQueue] = m.queueMs;
        o[kMPalm] = m.palmMs;
        o[kMLandmark] = m.landmarkMs;
        o[kMClassify] = m.classifyMs;
        o[kMSmooth] = m.smoothMs;
        o[kMControl] = m.controlMs;
        o[kMTotal] = m.totalMs;
        o[kMLatency] = m.latencyMs;
        o[kMRanPalm] = m.ranPalm;
        o[kMHand] = m.hand;
        o[kMSkipped] = m.skipped;
        o[kMRawCursor] = m.rawCursor.x;
        o[kMRawCursor + 1] = m.rawCursor.y;
        o[kMSmoothCursor] = m.smoothCursor.x;
        o[kMSmoothCursor + 1] = m.smoothCursor.y;
        o[kMRawPoint] = m.rawPoint.x;
        o[kMRawPoint + 1] = m.rawPoint.y;
        o[kMSmoothPoint] = m.smoothPoint.x;
        o[kMSmoothPoint + 1] = m.smoothPoint.y;
    }
    jdoubleArray out = env->NewDoubleArray(static_cast<jsize>(v.size()));
    if (out) env->SetDoubleArrayRegion(out, 0, static_cast<jsize>(v.size()), v.data());
    return out;
}

// Copies the frame of the last snapshot() into an RGBA_8888 bitmap of the same size.
jboolean copyFrame(JNIEnv* env, jobject, jobject bitmap) {
    const Frame& f = engine().snap.frame;
    AndroidBitmapInfo info{};
    if (f.empty() || AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
        info.format != ANDROID_BITMAP_FORMAT_RGBA_8888 || static_cast<int>(info.width) != f.width ||
        static_cast<int>(info.height) != f.height)
        return JNI_FALSE;
    void* pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS) return JNI_FALSE;
    for (int y = 0; y < f.height; ++y) {
        const uint8_t* s = f.row(y);
        auto* d = static_cast<uint8_t*>(pixels) + static_cast<size_t>(y) * info.stride;
        for (int x = 0; x < f.width; ++x, s += 4, d += 4) {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = 255;
        }
    }
    AndroidBitmap_unlockPixels(env, bitmap);
    return JNI_TRUE;
}

// [model status, ONNX Runtime version, recording file] from the last snapshot().
jobjectArray info(JNIEnv* env, jobject) {
    const Snapshot& s = engine().snap;
    const std::string values[] = {s.modelStatus, s.ortVersion, s.recordingPath};
    jobjectArray out = env->NewObjectArray(3, env->FindClass("java/lang/String"), nullptr);
    for (int i = 0; i < 3; ++i) {
        jstring v = jstr(env, values[i]);
        env->SetObjectArrayElement(out, i, v);
        env->DeleteLocalRef(v);
    }
    return out;
}

void startRecording(JNIEnv* env, jobject, jint label, jdouble delaySeconds, jdouble seconds, jstring folder) {
    if (label < 0 || label >= kNumPoses) return;
    engine().pipeline.startRecording(static_cast<Pose>(label), delaySeconds, seconds, toWide(str(env, folder)));
}

void stopRecording(JNIEnv*, jobject) { engine().pipeline.stopRecording(); }

const JNINativeMethod kMethods[] = {
    {"init", "(Ljava/lang/String;)Ljava/lang/String;", reinterpret_cast<void*>(nativeInit)},
    {"defaultSettings", "()Ljava/lang/String;", reinterpret_cast<void*>(defaultSettings)},
    {"normalizeSettings", "(Ljava/lang/String;)Ljava/lang/String;", reinterpret_cast<void*>(normalizeSettings)},
    {"start", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", reinterpret_cast<void*>(start)},
    {"stop", "()V", reinterpret_cast<void*>(stop)},
    {"submitFrame", "(Ljava/nio/ByteBuffer;IIII)V", reinterpret_cast<void*>(submitFrame)},
    {"updateSettings", "(Ljava/lang/String;)V", reinterpret_cast<void*>(updateSettings)},
    {"setScreen", "(III)V", reinterpret_cast<void*>(nativeSetScreen)},
    {"setEnabled", "(Z)V", reinterpret_cast<void*>(setEnabled)},
    {"toggleEnabled", "()V", reinterpret_cast<void*>(toggleEnabled)},
    {"status", "([I)V", reinterpret_cast<void*>(status)},
    {"snapshot", "()[D", reinterpret_cast<void*>(snapshot)},
    {"copyFrame", "(Landroid/graphics/Bitmap;)Z", reinterpret_cast<void*>(copyFrame)},
    {"info", "()[Ljava/lang/String;", reinterpret_cast<void*>(info)},
    {"startRecording", "(IDDLjava/lang/String;)V", reinterpret_cast<void*>(startRecording)},
    {"stopRecording", "()V", reinterpret_cast<void*>(stopRecording)},
};

}  // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass cls = env->FindClass("io/github/mahdidigitalx_oss/mausely/NativeEngine");
    if (!cls || env->RegisterNatives(cls, kMethods, sizeof kMethods / sizeof kMethods[0]) != JNI_OK) return JNI_ERR;
    if (!initInjector(vm, env, cls)) return JNI_ERR;
    return JNI_VERSION_1_6;
}
