package io.github.mahdidigitalx_oss.mausely

import android.accessibilityservice.AccessibilityService
import android.os.Handler
import android.os.SystemClock
import android.view.ViewConfiguration
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * Turns the pipeline's mouse actions into what a finger would do on a touch screen:
 *
 * - move: the on-screen cursor follows the hand;
 * - left button (thumb + index pinch): the finger touches down at the cursor and
 *   lifts on release, so a quick pinch taps, a held pinch long-presses and a
 *   moving pinch drags;
 * - right button (thumb + middle pinch): [MiddlePinchAction], Back by default;
 * - wheel (V sign): the finger drags the page from the cursor position.
 *
 * Runs on the main thread.
 */
class GestureDispatcher(
    private val service: AccessibilityService,
    private val cursor: CursorView,
    private val handler: Handler,
) {
    private val finger = Finger(service, handler)
    private val touchSlop = ViewConfiguration.get(service).scaledTouchSlop
    private var x = 0
    private var y = 0
    private var touching = false

    // Scrolling: the finger goes down only once the hand has moved past the touch
    // slop (so a V sign never taps) and lifts after a short pause (so it never flings).
    private var scrolling = false
    private var scrollX = 0f
    private var scrollY = 0f
    private var pendingX = 0f
    private var pendingY = 0f
    private var lastScrollMove = 0L
    private var lastWheel = 0L
    private val liftScroll = object : Runnable {
        override fun run() {
            val idle = SystemClock.uptimeMillis() - lastScrollMove
            if (idle < SCROLL_RELEASE_MS) {
                handler.postDelayed(this, SCROLL_RELEASE_MS - idle)
            } else {
                endScroll()
            }
        }
    }

    fun handle(actions: IntArray) {
        for (i in 0 until actions.size / 4) {
            val type = actions[i * 4]
            val amount = actions[i * 4 + 3]
            when (type) {
                Action.MOVE -> {
                    x = actions[i * 4 + 1].coerceIn(0, maxX())
                    y = actions[i * 4 + 2].coerceIn(0, maxY())
                    cursor.moveTo(x, y)
                    if (touching) finger.moveTo(x, y)
                }
                Action.LEFT_DOWN -> {
                    endScroll()
                    finger.down(x, y)
                    touching = true
                    cursor.touching = true
                }
                Action.LEFT_UP -> if (touching) {
                    finger.up()
                    touching = false
                    cursor.touching = false
                }
                Action.RIGHT_DOWN -> {
                    endScroll()
                    middlePinch()
                }
                Action.WHEEL -> scroll(0, amount)
                Action.H_WHEEL -> scroll(amount, 0)
            }
        }
    }

    /** Lifts the finger and forgets everything (service going away). */
    fun release() {
        handler.removeCallbacks(liftScroll)
        scrolling = false
        touching = false
        cursor.touching = false
        cursor.scrolling = false
        finger.release()
    }

    private fun middlePinch() {
        cursor.pulse()
        when (AppPrefs.middlePinch.value) {
            MiddlePinchAction.BACK -> service.performGlobalAction(AccessibilityService.GLOBAL_ACTION_BACK)
            MiddlePinchAction.HOME -> service.performGlobalAction(AccessibilityService.GLOBAL_ACTION_HOME)
            MiddlePinchAction.RECENTS -> service.performGlobalAction(AccessibilityService.GLOBAL_ACTION_RECENTS)
            MiddlePinchAction.NOTIFICATIONS -> service.performGlobalAction(AccessibilityService.GLOBAL_ACTION_NOTIFICATIONS)
            MiddlePinchAction.LONG_PRESS -> finger.press(x, y, ViewConfiguration.getLongPressTimeout() + 300L)
            MiddlePinchAction.NONE -> Unit
        }
    }

    // dx > 0: wheel right, dy > 0: wheel up (hand moved up). With natural scrolling
    // the page follows the hand, like dragging it with a finger.
    private fun scroll(dx: Int, dy: Int) {
        if (touching) return
        val now = SystemClock.uptimeMillis()
        if (!scrolling && now - lastWheel > SCROLL_RELEASE_MS) {
            pendingX = 0f  // a new scroll: forget small movements from the previous one
            pendingY = 0f
        }
        lastWheel = now
        val sign = if (AppPrefs.naturalScroll.value) 1f else -1f
        pendingX += dx * SCROLL_PX_PER_UNIT * sign
        pendingY += -dy * SCROLL_PX_PER_UNIT * sign
        // The finger stays a touch slop away from the screen edges; there it stops, lifts
        // after the pause and starts again at the cursor.
        val margin = touchSlop.toFloat()
        if (maxX() <= 2 * margin || maxY() <= 2 * margin) return  // overlay not laid out yet
        if (!scrolling) {
            if (abs(pendingX) < touchSlop && abs(pendingY) < touchSlop) return
            scrolling = true
            scrollX = x.toFloat().coerceIn(margin, maxX() - margin)
            scrollY = y.toFloat().coerceIn(margin, maxY() - margin)
            finger.down(scrollX.roundToInt(), scrollY.roundToInt())
            cursor.scrolling = true
            handler.postDelayed(liftScroll, SCROLL_RELEASE_MS)
        }
        val nx = (scrollX + pendingX).coerceIn(margin, maxX() - margin)
        val ny = (scrollY + pendingY).coerceIn(margin, maxY() - margin)
        pendingX = 0f
        pendingY = 0f
        if (nx.roundToInt() == scrollX.roundToInt() && ny.roundToInt() == scrollY.roundToInt()) return
        scrollX = nx
        scrollY = ny
        finger.moveTo(nx.roundToInt(), ny.roundToInt())
        lastScrollMove = now
    }

    private fun endScroll() {
        pendingX = 0f
        pendingY = 0f
        if (!scrolling) return
        handler.removeCallbacks(liftScroll)
        scrolling = false
        cursor.scrolling = false
        finger.up()
    }

    private fun maxX() = (cursor.screenWidth - 1).coerceAtLeast(0)
    private fun maxY() = (cursor.screenHeight - 1).coerceAtLeast(0)

    private companion object {
        const val SCROLL_PX_PER_UNIT = 1f  // finger pixels per wheel unit (settings: scroll speed)
        const val SCROLL_RELEASE_MS = 150L
    }
}
