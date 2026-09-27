package io.github.mahdidigitalx_oss.mausely

import android.graphics.Bitmap
import java.nio.ByteBuffer

/**
 * JNI bridge to the shared C++ pipeline (src/ and app/src/main/cpp/jni_bridge.cpp).
 * There is one pipeline per process; [Engine] owns its lifecycle.
 */
object NativeEngine {
    init {
        System.loadLibrary("mausely")
    }

    /** Receives the mouse actions of each processed frame; see [onActions]. */
    fun interface ActionSink {
        fun onActions(actions: IntArray)
    }

    @Volatile
    var sink: ActionSink? = null

    /**
     * Called by the C++ input backend on the pipeline thread with actions packed as
     * `[type, x, y, amount]` (types in [Action]). Only hand the array off from here:
     * calling back into NativeEngine could deadlock with [stop].
     */
    @JvmStatic
    fun onActions(actions: IntArray) {
        sink?.onActions(actions)
    }

    /** Loads ONNX Runtime and opens the log file. Returns an error message or null. */
    external fun init(logFile: String): String?

    /** Settings text (`key=value` lines, the desktop settings.ini format) with all defaults. */
    external fun defaultSettings(): String

    /** Applies [text] on top of the defaults and returns the complete settings text. */
    external fun normalizeSettings(text: String): String

    /** Loads the models from [modelDir] (with a trailing slash) and starts processing. Returns an error or null. */
    external fun start(modelDir: String, settings: String): String?

    external fun stop()

    /** Hands a camera frame (RGBA_8888, rotated clockwise by [rotation] degrees) to the pipeline. */
    external fun submitFrame(rgba: ByteBuffer, width: Int, height: Int, rowStride: Int, rotation: Int)

    external fun updateSettings(settings: String)

    /** Display size the active region maps to, and the system double-tap timeout. */
    external fun setScreen(width: Int, height: Int, doubleTapMs: Int)

    external fun setEnabled(on: Boolean)

    external fun toggleEnabled()

    /** Fills `[enabled, hand, frozen, pose, fistProgress * 1000]` without consuming snapshot metrics. */
    external fun status(out: IntArray)

    /** Latest pipeline state; the layout is described in [Snapshot]. Consumes the frame metrics. */
    external fun snapshot(): DoubleArray

    /** Copies the camera frame of the last [snapshot] into an ARGB_8888 bitmap of the same size. */
    external fun copyFrame(bitmap: Bitmap): Boolean

    /** `[model status, ONNX Runtime version, recording file]` of the last [snapshot]. */
    external fun info(): Array<String>

    external fun startRecording(label: Int, delaySeconds: Double, seconds: Double, folder: String)

    external fun stopRecording()
}

/** Mouse action types (control/mouse_action.h). Wheel amounts use 120 per notch; positive = up / right. */
object Action {
    const val MOVE = 0
    const val LEFT_DOWN = 1
    const val LEFT_UP = 2
    const val RIGHT_DOWN = 3
    const val RIGHT_UP = 4
    const val WHEEL = 5
    const val H_WHEEL = 6
}

/** Gesture classes of the gesture AI (ai/gesture_classifier.h); [NONE] = no hand. */
object Pose {
    const val MOVE = 0
    const val PINCH_INDEX = 1
    const val PINCH_MIDDLE = 2
    const val SCROLL = 3
    const val FIST = 4
    const val NONE = 5
    const val COUNT = 5
}
