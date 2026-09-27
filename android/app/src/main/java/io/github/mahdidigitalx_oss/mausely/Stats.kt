package io.github.mahdidigitalx_oss.mausely

import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sqrt

/** Fixed-size window of samples with exact mean / percentile / max (core/rolling_stats.h). */
class RollingStats(private val capacity: Int) {
    private val buf = DoubleArray(capacity)
    private var head = 0
    var count = 0
        private set

    fun push(v: Double) {
        buf[head] = v
        head = (head + 1) % capacity
        if (count < capacity) count++
    }

    /** i = 0 is the oldest sample still in the window. */
    operator fun get(i: Int) = buf[(head - count + i + capacity * 2) % capacity]

    fun mean() = if (count == 0) 0.0 else (0 until count).sumOf { get(it) } / count

    fun max() = if (count == 0) 0.0 else (0 until count).maxOf { get(it) }

    /** Nearest-rank percentile, p in [0, 100]. */
    fun percentile(p: Double): Double {
        if (count == 0) return 0.0
        val sorted = DoubleArray(count) { get(it) }.apply { sort() }
        return sorted[min((p / 100.0 * (count - 1)).roundToInt(), count - 1)]
    }
}

/** Per-stage timings, camera rate, hand visibility and cursor jitter, as on the desktop dashboard. */
class DashboardStats {
    enum class Stage { WAIT, PALM, LANDMARKS, GESTURE, SMOOTHING, CONTROL, TOTAL, LATENCY }

    val stages = Stage.entries.associateWith { RollingStats(300) }
    private val frameInterval = RollingStats(120)
    private val handRate = RollingStats(300)
    val jitterRaw = RollingStats(300)
    val jitterSmooth = RollingStats(300)
    private val recent = ArrayDeque<FrameMetrics>()
    private var lastArrivalMs = 0.0

    /** The newest frame with a hand (for drawing the control point), or null. */
    var lastHand: FrameMetrics? = null
        private set

    val cameraFps get() = if (frameInterval.count > 0) 1000.0 / frameInterval.mean() else 0.0
    val handVisiblePercent get() = handRate.mean() * 100.0

    fun ingest(m: FrameMetrics) {
        if (lastArrivalMs > 0 && m.arrivalMs > lastArrivalMs) frameInterval.push(m.arrivalMs - lastArrivalMs)
        lastArrivalMs = m.arrivalMs
        stages.getValue(Stage.WAIT).push(m.queueMs)
        if (m.ranPalm) stages.getValue(Stage.PALM).push(m.palmMs)
        if (m.landmarkMs > 0) stages.getValue(Stage.LANDMARKS).push(m.landmarkMs)
        if (m.hand) {
            stages.getValue(Stage.GESTURE).push(m.classifyMs)
            stages.getValue(Stage.SMOOTHING).push(m.smoothMs)
        }
        stages.getValue(Stage.CONTROL).push(m.controlMs)
        stages.getValue(Stage.TOTAL).push(m.totalMs)
        stages.getValue(Stage.LATENCY).push(m.latencyMs)
        handRate.push(if (m.hand) 1.0 else 0.0)

        if (!m.hand) {
            recent.clear()
            lastHand = null
            return
        }
        lastHand = m
        // Jitter while the hand is (nearly) still: RMS frame-to-frame motion over the
        // last 15 frames when the raw point stayed within 15 px.
        recent.addLast(m)
        if (recent.size > JITTER_WINDOW) recent.removeFirst()
        val first = recent.first()
        if (recent.size == JITTER_WINDOW && hypot(m.rawCursorX - first.rawCursorX, m.rawCursorY - first.rawCursorY) < 15f) {
            var raw = 0.0
            var smooth = 0.0
            for (i in 1 until recent.size) {
                val a = recent[i - 1]
                val b = recent[i]
                raw += sq(hypot(b.rawCursorX - a.rawCursorX, b.rawCursorY - a.rawCursorY))
                smooth += sq(hypot(b.smoothCursorX - a.smoothCursorX, b.smoothCursorY - a.smoothCursorY))
            }
            jitterRaw.push(sqrt(raw / (JITTER_WINDOW - 1)))
            jitterSmooth.push(sqrt(smooth / (JITTER_WINDOW - 1)))
        }
    }

    private fun sq(v: Float) = v.toDouble() * v

    private companion object {
        const val JITTER_WINDOW = 15
    }
}
