package io.github.mahdidigitalx_oss.mausely

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** What a thumb + middle finger pinch (a right click on the desktop) does on Android. */
enum class MiddlePinchAction { BACK, LONG_PRESS, HOME, RECENTS, NOTIFICATIONS, NONE }

/** Android-only preferences (the shared pipeline settings live in [Engine.settings]). */
object AppPrefs {
    private lateinit var prefs: SharedPreferences

    private val _middlePinch = MutableStateFlow(MiddlePinchAction.BACK)
    val middlePinch: StateFlow<MiddlePinchAction> = _middlePinch.asStateFlow()

    private val _naturalScroll = MutableStateFlow(true)
    /** The page follows the hand, like dragging it with a finger. Off = mouse-wheel direction. */
    val naturalScroll: StateFlow<Boolean> = _naturalScroll.asStateFlow()

    private val _cursorScale = MutableStateFlow(1f)
    val cursorScale: StateFlow<Float> = _cursorScale.asStateFlow()

    fun init(context: Context) {
        prefs = context.getSharedPreferences("android", Context.MODE_PRIVATE)
        _middlePinch.value = prefs.getString("middle_pinch", null)
            ?.let { name -> MiddlePinchAction.entries.firstOrNull { it.name == name } } ?: MiddlePinchAction.BACK
        _naturalScroll.value = prefs.getBoolean("natural_scroll", true)
        _cursorScale.value = prefs.getFloat("cursor_scale", 1f)
    }

    fun setMiddlePinch(action: MiddlePinchAction) {
        _middlePinch.value = action
        prefs.edit { putString("middle_pinch", action.name) }
    }

    fun setNaturalScroll(on: Boolean) {
        _naturalScroll.value = on
        prefs.edit { putBoolean("natural_scroll", on) }
    }

    fun setCursorScale(scale: Float) {
        _cursorScale.value = scale
        prefs.edit { putFloat("cursor_scale", scale) }
    }

    fun reset() {
        setMiddlePinch(MiddlePinchAction.BACK)
        setNaturalScroll(true)
        setCursorScale(1f)
    }
}
