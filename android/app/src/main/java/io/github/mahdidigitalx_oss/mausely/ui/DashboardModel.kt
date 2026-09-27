package io.github.mahdidigitalx_oss.mausely.ui

import android.graphics.Bitmap
import android.os.Process
import android.os.SystemClock
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.core.graphics.createBitmap
import io.github.mahdidigitalx_oss.mausely.DashboardStats
import io.github.mahdidigitalx_oss.mausely.NativeEngine
import io.github.mahdidigitalx_oss.mausely.Snapshot

/** Pulls pipeline snapshots for the dashboard (main thread, once per display frame). */
class DashboardModel {
    val stats = DashboardStats()
    var snapshot by mutableStateOf<Snapshot?>(null)
        private set
    /** The newest camera frame; a new wrapper around the same reused bitmap each time its pixels change. */
    var frame by mutableStateOf<ImageBitmap?>(null)
        private set
    var cpuPercent by mutableDoubleStateOf(0.0)
        private set
    var modelStatus by mutableStateOf("")
        private set
    var ortVersion by mutableStateOf("")
        private set
    var recordingPath by mutableStateOf("")
        private set

    private var bitmap: Bitmap? = null
    private var cpuWallMs = 0L
    private var cpuProcessMs = 0L
    private var infoMs = 0L

    fun poll() {
        val s = Snapshot(NativeEngine.snapshot())
        if (s.metrics.isEmpty() && !s.newFrame && snapshot != null) return  // nothing new: skip recomposition
        s.metrics.forEach(stats::ingest)
        if (s.newFrame && s.frameWidth > 0 && s.frameHeight > 0) {
            val b = bitmap?.takeIf { it.width == s.frameWidth && it.height == s.frameHeight }
                ?: createBitmap(s.frameWidth, s.frameHeight).also { bitmap = it }
            if (NativeEngine.copyFrame(b)) frame = b.asImageBitmap()
        }
        snapshot = s

        val now = SystemClock.elapsedRealtime()
        if (now - infoMs >= 1000) {
            infoMs = now
            val info = NativeEngine.info()
            modelStatus = info[0]
            ortVersion = info[1]
            recordingPath = info[2]
            // Share of all cores used by this process since the last update.
            val cpu = Process.getElapsedCpuTime()
            if (cpuWallMs > 0) {
                val wall = (now - cpuWallMs) * Runtime.getRuntime().availableProcessors()
                cpuPercent = 100.0 * (cpu - cpuProcessMs) / wall.coerceAtLeast(1)
            }
            cpuWallMs = now
            cpuProcessMs = cpu
        }
    }

    fun clearFrame() {
        frame = null
        bitmap = null
        snapshot = null
    }
}
