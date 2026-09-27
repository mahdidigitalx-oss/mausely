package io.github.mahdidigitalx_oss.mausely

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.GestureDescription
import android.graphics.Path
import android.os.Handler

/**
 * One simulated finger, built from continued accessibility gestures so that a
 * touch can stay down for as long as the pinch is held (tap, long press, drag).
 *
 * Only one gesture part can be in flight: requests are queued and consecutive
 * moves are merged, so the finger never falls behind the hand. Coordinates are
 * whole pixels, which keeps each part starting exactly where the last one ended
 * (the system cancels the gesture otherwise).
 */
class Finger(private val service: AccessibilityService, private val handler: Handler) {
    private sealed interface Op
    private data class Down(val x: Int, val y: Int) : Op
    private data class Move(val x: Int, val y: Int) : Op
    private data object Up : Op
    private data class Press(val x: Int, val y: Int, val durationMs: Long) : Op

    private val queue = ArrayDeque<Op>()
    private var stroke: GestureDescription.StrokeDescription? = null  // the last part, while the finger is down
    private var endX = 0
    private var endY = 0
    private var busy = false

    fun down(x: Int, y: Int) = enqueue(Down(x, y))

    fun moveTo(x: Int, y: Int) = enqueue(Move(x, y))

    fun up() = enqueue(Up)

    /** A single touch of fixed length (long press). */
    fun press(x: Int, y: Int, durationMs: Long) = enqueue(Press(x, y, durationMs))

    /** Drops queued requests; a finger that is down is lifted. */
    fun release() {
        queue.clear()
        if (stroke != null) queue.add(Up)
        pump()
    }

    private fun enqueue(op: Op) {
        queue.add(op)
        pump()
    }

    private fun pump() {
        while (!busy && queue.isNotEmpty()) {
            val part = nextPart() ?: continue
            stroke = if (part.willContinue()) part else null
            busy = true
            val dispatched = service.dispatchGesture(
                GestureDescription.Builder().addStroke(part).build(),
                object : AccessibilityService.GestureResultCallback() {
                    override fun onCompleted(gestureDescription: GestureDescription) {
                        busy = false
                        pump()
                    }

                    override fun onCancelled(gestureDescription: GestureDescription) {
                        interrupted()
                        pump()
                    }
                },
                handler,
            )
            if (!dispatched) interrupted()
        }
    }

    // Turns the head of the queue into the next gesture part (null: nothing to send for it).
    private fun nextPart(): GestureDescription.StrokeDescription? {
        val current = stroke
        return when (val op = queue.removeFirst()) {
            is Down, is Press -> {
                if (current != null) {  // still down: lift first
                    queue.addFirst(op)
                    queue.addFirst(Up)
                    return null
                }
                if (op is Down) {
                    endX = op.x
                    endY = op.y
                    GestureDescription.StrokeDescription(point(op.x, op.y), 0, TOUCH_MS, true)
                } else {
                    op as Press
                    GestureDescription.StrokeDescription(point(op.x, op.y), 0, op.durationMs, false)
                }
            }
            is Move -> {
                var target = op
                while (queue.firstOrNull() is Move) target = queue.removeFirst() as Move  // merge moves
                if (current == null || (target.x == endX && target.y == endY)) return null
                val path = Path().apply {
                    moveTo(endX.toFloat(), endY.toFloat())
                    lineTo(target.x.toFloat(), target.y.toFloat())
                }
                endX = target.x
                endY = target.y
                current.continueStroke(path, 0, MOVE_MS, true)
            }
            Up -> current?.continueStroke(point(endX, endY), 0, TOUCH_MS, false)
        }
    }

    // A real touch on the screen (or the system) interrupted the gesture: the finger is up.
    private fun interrupted() {
        busy = false
        stroke = null
        while (queue.firstOrNull().let { it is Move || it is Up }) queue.removeFirst()
    }

    private fun point(x: Int, y: Int) = Path().apply { moveTo(x.toFloat(), y.toFloat()) }

    private companion object {
        const val TOUCH_MS = 10L  // finger down / up parts
        const val MOVE_MS = 16L   // one display frame per movement part
    }
}
