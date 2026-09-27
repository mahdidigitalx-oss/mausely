package io.github.mahdidigitalx_oss.mausely

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StatsTest {
    @Test
    fun rollingStatsMatchesTheCppVersion() {
        val s = RollingStats(100)
        for (i in 1..100) s.push(i.toDouble())
        assertEquals(100, s.count)
        assertEquals(50.5, s.mean(), 1e-9)
        assertEquals(50.0, s.percentile(50.0), 1.0)
        assertEquals(95.0, s.percentile(95.0), 1.0)
        assertEquals(100.0, s.max(), 0.0)
    }

    @Test
    fun rollingStatsEvictsTheOldestSamples() {
        val s = RollingStats(3)
        for (i in 1..5) s.push(i.toDouble())
        assertEquals(3, s.count)
        assertEquals(3.0, s[0], 0.0)
        assertEquals(5.0, s[2], 0.0)
        assertEquals(4.0, s.mean(), 1e-9)
    }

    @Test
    fun dashboardMeasuresRateAndJitterOfAStillHand() {
        val stats = DashboardStats()
        for (i in 0 until 60) {
            val wobble = if (i % 2 == 0) 2f else -2f  // raw point jitters by 4 px, the smoothed one by 1 px
            stats.ingest(metrics(arrivalMs = i * 33.3, rawX = 500f + wobble, smoothX = 500f + wobble / 4))
        }
        assertEquals(30.0, stats.cameraFps, 0.1)
        assertEquals(100.0, stats.handVisiblePercent, 1e-9)
        assertEquals(4.0, stats.jitterRaw.mean(), 1e-3)
        assertEquals(1.0, stats.jitterSmooth.mean(), 1e-3)
        assertTrue(stats.lastHand != null)

        stats.ingest(metrics(arrivalMs = 60 * 33.3, hand = false))
        assertEquals(null, stats.lastHand)
    }

    private fun metrics(arrivalMs: Double, rawX: Float = 0f, smoothX: Float = 0f, hand: Boolean = true) = FrameMetrics(
        arrivalMs = arrivalMs, queueMs = 1.0, palmMs = 0.0, landmarkMs = 8.0, classifyMs = 0.1, smoothMs = 0.05,
        controlMs = 0.01, totalMs = 9.0, latencyMs = 10.0, ranPalm = false, hand = hand, skipped = false,
        rawCursorX = rawX, rawCursorY = 300f, smoothCursorX = smoothX, smoothCursorY = 300f,
        rawPointX = 0f, rawPointY = 0f, smoothPointX = 0f, smoothPointY = 0f,
    )
}
