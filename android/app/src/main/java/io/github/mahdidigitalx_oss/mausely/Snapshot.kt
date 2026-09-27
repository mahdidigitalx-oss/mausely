package io.github.mahdidigitalx_oss.mausely

/**
 * Read-only view of the array returned by [NativeEngine.snapshot].
 * The indices mirror the constants at the top of jni_bridge.cpp.
 */
class Snapshot(private val v: DoubleArray) {
    val frameWidth get() = v[FRAME_WIDTH].toInt()
    val frameHeight get() = v[FRAME_HEIGHT].toInt()
    val newFrame get() = v[NEW_FRAME] != 0.0
    val handValid get() = v[HAND_VALID] != 0.0
    val handTracked get() = v[HAND_TRACKED] != 0.0
    val presence get() = v[PRESENCE].toFloat()
    val rightHand get() = v[HANDEDNESS] > 0.5

    /** Tracking region: centre x, centre y, side, angle (radians), in frame pixels. */
    fun roi(i: Int) = v[ROI + i].toFloat()
    fun landmarkX(i: Int) = v[LANDMARKS + 2 * i].toFloat()
    fun landmarkY(i: Int) = v[LANDMARKS + 2 * i + 1].toFloat()
    fun prob(pose: Int) = v[PROBS + pose].toFloat()

    val pose get() = v[POSE].toInt()
    val enabled get() = v[ENABLED] != 0.0
    val frozen get() = v[FROZEN] != 0.0
    val fistProgress get() = v[FIST_PROGRESS].toFloat()

    val leftClicks get() = v[COUNTERS].toInt()
    val doubleClicks get() = v[COUNTERS + 1].toInt()
    val rightClicks get() = v[COUNTERS + 2].toInt()
    val drags get() = v[COUNTERS + 3].toInt()
    val toggles get() = v[COUNTERS + 4].toInt()
    val scrollUnits get() = v[COUNTERS + 5].toLong()

    val droppedFrames get() = v[DROPPED].toLong()
    val classifierReady get() = v[CLASSIFIER_READY] != 0.0
    val aiSmootherReady get() = v[AI_SMOOTHER_READY] != 0.0
    val recording get() = v[RECORDING] != 0.0
    val recordingWaiting get() = v[RECORDING_WAITING] != 0.0
    val recordingSecondsLeft get() = v[RECORDING_SECONDS_LEFT]
    val recordedSamples get() = v[RECORDED_SAMPLES].toInt()

    /** Timing and results of the frames processed since the previous snapshot. */
    val metrics: List<FrameMetrics> = List(v[METRIC_COUNT].toInt()) { i ->
        val o = HEADER_SIZE + i * METRIC_SIZE
        FrameMetrics(
            arrivalMs = v[o],
            queueMs = v[o + 1],
            palmMs = v[o + 2],
            landmarkMs = v[o + 3],
            classifyMs = v[o + 4],
            smoothMs = v[o + 5],
            controlMs = v[o + 6],
            totalMs = v[o + 7],
            latencyMs = v[o + 8],
            ranPalm = v[o + 9] != 0.0,
            hand = v[o + 10] != 0.0,
            skipped = v[o + 11] != 0.0,
            rawCursorX = v[o + 12].toFloat(),
            rawCursorY = v[o + 13].toFloat(),
            smoothCursorX = v[o + 14].toFloat(),
            smoothCursorY = v[o + 15].toFloat(),
            rawPointX = v[o + 16].toFloat(),
            rawPointY = v[o + 17].toFloat(),
            smoothPointX = v[o + 18].toFloat(),
            smoothPointY = v[o + 19].toFloat(),
        )
    }

    private companion object {
        const val FRAME_WIDTH = 0
        const val FRAME_HEIGHT = 1
        const val NEW_FRAME = 2
        const val HAND_VALID = 3
        const val HAND_TRACKED = 4
        const val PRESENCE = 5
        const val HANDEDNESS = 6
        const val ROI = 7
        const val LANDMARKS = 11
        const val PROBS = 53
        const val POSE = 58
        const val ENABLED = 59
        const val FROZEN = 60
        const val FIST_PROGRESS = 61
        const val COUNTERS = 62
        const val DROPPED = 68
        const val CLASSIFIER_READY = 69
        const val AI_SMOOTHER_READY = 70
        const val RECORDING = 71
        const val RECORDING_WAITING = 72
        const val RECORDING_SECONDS_LEFT = 73
        const val RECORDED_SAMPLES = 74
        const val METRIC_COUNT = 75
        const val HEADER_SIZE = 76
        const val METRIC_SIZE = 20
    }
}

/** One processed camera frame (pipeline/pipeline.h). Points are in frame-height units, cursors in screen pixels. */
data class FrameMetrics(
    val arrivalMs: Double,
    val queueMs: Double,
    val palmMs: Double,
    val landmarkMs: Double,
    val classifyMs: Double,
    val smoothMs: Double,
    val controlMs: Double,
    val totalMs: Double,
    val latencyMs: Double,
    val ranPalm: Boolean,
    val hand: Boolean,
    val skipped: Boolean,
    val rawCursorX: Float,
    val rawCursorY: Float,
    val smoothCursorX: Float,
    val smoothCursorY: Float,
    val rawPointX: Float,
    val rawPointY: Float,
    val smoothPointX: Float,
    val smoothPointY: Float,
)
