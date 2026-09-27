package io.github.mahdidigitalx_oss.mausely

import android.accessibilityservice.AccessibilityService
import android.content.Intent
import android.graphics.PixelFormat
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.view.WindowManager
import android.view.accessibility.AccessibilityEvent
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * The accessibility service that acts on the hand: it draws the cursor overlay and
 * performs taps, drags, scrolls and global actions. Apps cannot inject input any
 * other way on Android. It reads no screen content.
 */
class ControlService : AccessibilityService() {
    private val handler = Handler(Looper.getMainLooper())
    private var scope: CoroutineScope? = null
    private var cursor: CursorView? = null
    private var dispatcher: GestureDispatcher? = null

    override fun onServiceConnected() {
        super.onServiceConnected()
        Engine.init(this)
        val view = CursorView(this)
        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.TYPE_ACCESSIBILITY_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE or
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            PixelFormat.TRANSLUCENT,
        )
        if (Build.VERSION.SDK_INT >= 30) {
            params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
        } else if (Build.VERSION.SDK_INT >= 28) {
            params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        getSystemService(WindowManager::class.java).addView(view, params)
        val gestures = GestureDispatcher(this, view, handler)
        cursor = view
        dispatcher = gestures
        NativeEngine.sink = NativeEngine.ActionSink { actions -> handler.post { gestures.handle(actions) } }
        scope = CoroutineScope(SupervisorJob() + Dispatchers.Main).apply {
            launch { Engine.running.collect { view.polling = it } }
        }
        _connected.value = true
    }

    override fun onUnbind(intent: Intent?): Boolean {
        tearDown()
        return super.onUnbind(intent)
    }

    override fun onDestroy() {
        tearDown()
        super.onDestroy()
    }

    private fun tearDown() {
        val view = cursor ?: return
        NativeEngine.sink = null
        scope?.cancel()
        scope = null
        view.polling = false
        dispatcher?.release()
        dispatcher = null
        getSystemService(WindowManager::class.java).removeView(view)
        cursor = null
        _connected.value = false
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) = Unit

    override fun onInterrupt() = Unit

    companion object {
        private val _connected = MutableStateFlow(false)
        /** The user has turned the service on in the system's accessibility settings. */
        val connected: StateFlow<Boolean> = _connected.asStateFlow()
    }
}
