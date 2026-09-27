package io.github.mahdidigitalx_oss.mausely

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.os.SystemClock
import android.view.Choreographer
import android.view.View
import androidx.core.graphics.withTranslation
import kotlin.math.min

/**
 * Full-screen, touch-transparent overlay that draws the hand cursor, its press /
 * scroll state and the fist-toggle progress. While [polling], it reads the
 * pipeline status on every display frame.
 */
class CursorView(context: Context) : View(context) {
    private val density = resources.displayMetrics.density
    private val arrow = Path().apply {
        // Classic pointer, tip at (0, 0), in dp.
        moveTo(0f, 0f)
        lineTo(0f, 17f)
        lineTo(4.2f, 13.1f)
        lineTo(6.9f, 19.3f)
        lineTo(9.6f, 18.1f)
        lineTo(6.9f, 12f)
        lineTo(12.4f, 12f)
        close()
    }
    private val chevrons = Path().apply {
        moveTo(15f, 7f); lineTo(18f, 3f); lineTo(21f, 7f)
        moveTo(15f, 12f); lineTo(18f, 16f); lineTo(21f, 12f)
    }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE }
    private val outline = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.BLACK
        style = Paint.Style.STROKE
        strokeJoin = Paint.Join.ROUND
        strokeCap = Paint.Cap.ROUND
    }
    private val accent = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = ACCENT }
    private val ring = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
    }
    private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 14f * density
        textAlign = Paint.Align.CENTER
    }
    private val labelBackground = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xCC202228.toInt() }
    private val arc = RectF()
    private val location = IntArray(2)

    private var cursorX = -1f
    private var cursorY = -1f
    private var lastMove = 0L
    private var pulseStart = 0L
    private var enabled = false
    private var fistProgress = 0f
    private var message: String? = null
    private var messageUntil = 0L
    private val status = IntArray(5)

    var touching = false
        set(value) {
            field = value
            invalidate()
        }
    var scrolling = false
        set(value) {
            field = value
            invalidate()
        }

    /** Display size (the overlay covers the whole display, possibly starting below a cutout). */
    val screenWidth get() = location[0] + width
    val screenHeight get() = location[1] + height

    var polling = false
        set(value) {
            if (field == value) return
            field = value
            if (value) {
                enabled = false
                Choreographer.getInstance().postFrameCallback(frame)
            } else {
                Choreographer.getInstance().removeFrameCallback(frame)
                fistProgress = 0f
                invalidate()
            }
        }

    private val frame = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            tick()
            if (polling) Choreographer.getInstance().postFrameCallback(this)
        }
    }

    fun moveTo(x: Int, y: Int) {
        cursorX = (x - location[0]).toFloat()
        cursorY = (y - location[1]).toFloat()
        lastMove = SystemClock.uptimeMillis()
        invalidate()
    }

    /** A short ring for one-off actions (Back, Home...). */
    fun pulse() {
        pulseStart = SystemClock.uptimeMillis()
        invalidate()
    }

    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        super.onLayout(changed, left, top, right, bottom)
        getLocationOnScreen(location)
    }

    private fun tick() {
        NativeEngine.status(status)
        val nowEnabled = status[0] == 1
        val now = SystemClock.uptimeMillis()
        if (nowEnabled != enabled) {
            enabled = nowEnabled
            message = context.getString(if (enabled) R.string.cursor_on else R.string.cursor_paused)
            messageUntil = now + MESSAGE_MS
            if (enabled) lastMove = now  // show the cursor where it is
        }
        val progress = status[4] / 1000f
        if (progress != fistProgress || cursorAlpha(now) > 0f || now < messageUntil || now - pulseStart < PULSE_MS) {
            fistProgress = progress
            invalidate()
        }
    }

    private fun cursorAlpha(now: Long): Float {
        if (!polling || !enabled || cursorX < 0f) return 0f
        if (touching || scrolling) return 1f
        val idle = now - lastMove
        return when {
            idle < HIDE_AFTER_MS -> 1f
            idle < HIDE_AFTER_MS + FADE_MS -> 1f - (idle - HIDE_AFTER_MS).toFloat() / FADE_MS
            else -> 0f
        }
    }

    override fun onDraw(canvas: Canvas) {
        val now = SystemClock.uptimeMillis()
        val x = if (cursorX >= 0f) cursorX else width / 2f
        val y = if (cursorY >= 0f) cursorY else height / 2f
        val scale = density * AppPrefs.cursorScale.value * 1.3f

        val alpha = cursorAlpha(now)
        if (alpha > 0f) {
            val a = (alpha * 255).toInt()
            if (touching) {
                accent.alpha = a * 110 / 255
                canvas.drawCircle(x, y, 12f * scale, accent)
            }
            canvas.withTranslation(x, y) {
                scale(scale, scale)
                fill.alpha = a
                outline.alpha = a
                outline.strokeWidth = 1.3f
                drawPath(arrow, fill)
                drawPath(arrow, outline)
                if (scrolling) {
                    outline.strokeWidth = 2.2f
                    drawPath(chevrons, outline)
                }
            }
        }

        val pulse = now - pulseStart
        if (pulse < PULSE_MS && polling) {
            val t = pulse.toFloat() / PULSE_MS
            ring.color = ACCENT
            ring.alpha = ((1f - t) * 255).toInt()
            ring.strokeWidth = 3f * density
            canvas.drawCircle(x, y, (8f + 20f * t) * scale, ring)
        }

        if (fistProgress > 0f) {
            val r = 22f * scale
            arc.set(x - r, y - r, x + r, y + r)
            ring.color = FIST
            ring.alpha = 255
            ring.strokeWidth = 4f * density
            canvas.drawArc(arc, -90f, 360f * fistProgress, false, ring)
        }

        val text = message
        if (text != null && now < messageUntil) {
            val tx = x.coerceIn(60f * density, width - 60f * density)
            val ty = min(y + 48f * scale, height - 24f * density)
            val half = label.measureText(text) / 2 + 10f * density
            canvas.drawRoundRect(tx - half, ty - 20f * density, tx + half, ty + 8f * density, 8f * density, 8f * density, labelBackground)
            canvas.drawText(text, tx, ty, label)
        }
    }

    private companion object {
        const val ACCENT = 0xFF3D9EF5.toInt()
        const val FIST = 0xFFFFC83C.toInt()
        const val HIDE_AFTER_MS = 2500L
        const val FADE_MS = 400L
        const val MESSAGE_MS = 1500L
        const val PULSE_MS = 350L
    }
}
