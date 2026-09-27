package io.github.mahdidigitalx_oss.mausely.ui

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RangeSlider
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import io.github.mahdidigitalx_oss.mausely.AppPrefs
import io.github.mahdidigitalx_oss.mausely.DashboardStats.Stage
import io.github.mahdidigitalx_oss.mausely.Engine
import io.github.mahdidigitalx_oss.mausely.MiddlePinchAction
import io.github.mahdidigitalx_oss.mausely.NativeEngine
import io.github.mahdidigitalx_oss.mausely.Pose
import io.github.mahdidigitalx_oss.mausely.R
import io.github.mahdidigitalx_oss.mausely.flag
import io.github.mahdidigitalx_oss.mausely.float
import io.github.mahdidigitalx_oss.mausely.int
import kotlin.math.roundToInt

@Composable
fun PerformanceTab(model: DashboardModel, settings: Map<String, String>) {
    val stats = model.stats
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        val pipeline = stats.stages.getValue(Stage.TOTAL).percentile(50.0)
        val latency = stats.stages.getValue(Stage.LATENCY).percentile(50.0)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Kpi(stringResource(R.string.kpi_camera_fps), "%.1f".format(stats.cameraFps), if (stats.cameraFps >= 24) Good else Warn)
            Kpi(stringResource(R.string.kpi_pipeline), "%.1f ms".format(pipeline), if (pipeline < 33) Good else Warn)
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Kpi(stringResource(R.string.kpi_latency), "%.1f ms".format(latency), if (latency < 50) Good else Warn)
            Kpi(stringResource(R.string.kpi_cpu), "%.0f %%".format(model.cpuPercent), if (model.cpuPercent < 35) Good else Warn)
        }

        val names = mapOf(
            Stage.WAIT to R.string.stage_wait, Stage.PALM to R.string.stage_palm,
            Stage.LANDMARKS to R.string.stage_landmarks, Stage.GESTURE to R.string.stage_gesture,
            Stage.SMOOTHING to R.string.stage_smoothing, Stage.CONTROL to R.string.stage_control,
            Stage.TOTAL to R.string.stage_total, Stage.LATENCY to R.string.stage_latency,
        )
        Column {
            StageRow(stringResource(R.string.stage), listOf("avg", "p50", "p95", "max"), header = true)
            HorizontalDivider()
            for ((stage, name) in names) {
                val s = stats.stages.getValue(stage)
                StageRow(stringResource(name), listOf(s.mean(), s.percentile(50.0), s.percentile(95.0), s.max()).map { "%.2f".format(it) })
            }
        }

        val snap = model.snapshot
        Text(
            stringResource(R.string.perf_hand_visible, stats.handVisiblePercent, snap?.droppedFrames ?: 0L),
            style = MaterialTheme.typography.bodyMedium,
        )
        if (stats.jitterRaw.count > 0) {
            val raw = stats.jitterRaw.mean()
            val smooth = stats.jitterSmooth.mean()
            val cut = if (raw > 0) 100 * (1 - smooth / raw) else 0.0
            Text(stringResource(R.string.perf_jitter, raw, smooth, cut), style = MaterialTheme.typography.bodyMedium)
        } else {
            Text(stringResource(R.string.perf_jitter_none), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        if (model.ortVersion.isNotEmpty()) {
            Text(
                stringResource(R.string.perf_runtime, model.ortVersion, settings.int("inference_threads", 2)),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        if (model.modelStatus.isNotEmpty()) Text(model.modelStatus, color = Warn, style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun RowScope.Kpi(caption: String, value: String, color: Color) {
    Card(
        modifier = Modifier.weight(1f),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerHigh),
    ) {
        Column(Modifier.padding(12.dp)) {
            Text(caption, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1)
            Text(value, style = MaterialTheme.typography.headlineSmall, color = color)
        }
    }
}

@Composable
private fun StageRow(name: String, values: List<String>, header: Boolean = false) {
    val style = if (header) MaterialTheme.typography.labelMedium else MaterialTheme.typography.bodySmall
    val color = if (header) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.onSurface
    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(name, Modifier.weight(1f), style = style, color = color, maxLines = 1)
        for (v in values) {
            Text(v, Modifier.width(52.dp), style = style, color = color, textAlign = TextAlign.End, fontFamily = FontFamily.Monospace)
        }
    }
}

@Composable
fun GesturesTab(model: DashboardModel) {
    val snap = model.snapshot
    val labels = poseLabels()
    val middle by AppPrefs.middlePinch.collectAsStateWithLifecycle()
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        if (snap != null && !snap.classifierReady) Text(stringResource(R.string.gesture_ai_missing), color = Warn)
        Text(
            stringResource(R.string.current_pose, labels[(snap?.pose ?: Pose.NONE).coerceIn(0, Pose.NONE)]),
            style = MaterialTheme.typography.titleMedium,
        )
        for (pose in 0 until Pose.COUNT) {
            val p = snap?.prob(pose) ?: 0f
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(labels[pose], Modifier.width(110.dp), style = MaterialTheme.typography.bodySmall, maxLines = 1)
                LinearProgressIndicator(
                    progress = { p },
                    modifier = Modifier.weight(1f),
                    color = if (snap?.pose == pose) Accent else MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text("%3.0f%%".format(p * 100), Modifier.width(44.dp), style = MaterialTheme.typography.bodySmall, textAlign = TextAlign.End)
            }
        }

        val counters = listOf(
            R.string.counter_taps to (snap?.leftClicks ?: 0).toLong(),
            R.string.counter_double to (snap?.doubleClicks ?: 0).toLong(),
            R.string.counter_middle to (snap?.rightClicks ?: 0).toLong(),
            R.string.counter_drags to (snap?.drags ?: 0).toLong(),
            R.string.counter_toggles to (snap?.toggles ?: 0).toLong(),
            R.string.counter_scroll to (snap?.scrollUnits ?: 0L),
        )
        for (row in counters.chunked(2)) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for ((name, value) in row) Kpi(stringResource(name), value.toString(), MaterialTheme.colorScheme.onSurface)
            }
        }

        Text(stringResource(R.string.guide_title), style = MaterialTheme.typography.titleMedium, modifier = Modifier.padding(top = 4.dp))
        val guide = listOf(
            stringResource(R.string.guide_move),
            stringResource(R.string.guide_tap),
            stringResource(R.string.guide_double),
            stringResource(R.string.guide_middle, stringResource(middleLabel(middle))),
            stringResource(R.string.guide_scroll),
            stringResource(R.string.guide_fist),
        )
        for (line in guide) Text("•  $line", style = MaterialTheme.typography.bodyMedium)
    }
}

private fun middleLabel(action: MiddlePinchAction) = when (action) {
    MiddlePinchAction.BACK -> R.string.middle_back
    MiddlePinchAction.LONG_PRESS -> R.string.middle_long_press
    MiddlePinchAction.HOME -> R.string.middle_home
    MiddlePinchAction.RECENTS -> R.string.middle_recents
    MiddlePinchAction.NOTIFICATIONS -> R.string.middle_notifications
    MiddlePinchAction.NONE -> R.string.middle_none
}

@Composable
fun SettingsTab(settings: Map<String, String>, model: DashboardModel) {
    val middle by AppPrefs.middlePinch.collectAsStateWithLifecycle()
    val naturalScroll by AppPrefs.naturalScroll.collectAsStateWithLifecycle()
    val cursorScale by AppPrefs.cursorScale.collectAsStateWithLifecycle()
    fun set(key: String, value: Any) = Engine.update(mapOf(key to value.toString()))

    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Section(stringResource(R.string.section_pointer))
        Label(stringResource(R.string.active_area_h), "%.2f – %.2f".format(settings.float("region_x0"), settings.float("region_x1")))
        RangeSlider(
            value = settings.float("region_x0")..settings.float("region_x1"),
            onValueChange = { if (it.endInclusive - it.start >= 0.1f) Engine.update(mapOf("region_x0" to "${it.start}", "region_x1" to "${it.endInclusive}")) },
        )
        Label(stringResource(R.string.active_area_v), "%.2f – %.2f".format(settings.float("region_y0"), settings.float("region_y1")))
        RangeSlider(
            value = settings.float("region_y0")..settings.float("region_y1"),
            onValueChange = { if (it.endInclusive - it.start >= 0.1f) Engine.update(mapOf("region_y0" to "${it.start}", "region_y1" to "${it.endInclusive}")) },
        )
        Hint(stringResource(R.string.active_area_hint))

        Label(stringResource(R.string.smoothing))
        val smoothing = settings.int("smoothing", 2)
        Chips(
            listOf(R.string.smoothing_none, R.string.smoothing_one_euro, R.string.smoothing_ai, R.string.smoothing_ai_predict)
                .map { stringResource(it) },
            selected = smoothing,
            enabled = { it < 2 || model.snapshot?.aiSmootherReady != false },
        ) { set("smoothing", it) }
        if (smoothing == 3) {
            SliderRow(stringResource(R.string.predict_strength), settings.float("predict_strength"), 0f..1f, "%.2f") {
                set("predict_strength", it)
            }
        }
        SliderRow(stringResource(R.string.cursor_size), cursorScale, 0.6f..2f, "%.1f×", onChange = AppPrefs::setCursorScale)

        Section(stringResource(R.string.section_touch))
        SliderRow(stringResource(R.string.click_rewind), settings.float("click_rewind_us") / 1000f, 0f..250f, "%.0f ms") {
            set("click_rewind_us", (it * 1000).roundToInt())
        }
        SliderRow(stringResource(R.string.drag_threshold), settings.float("drag_threshold_px"), 5f..80f, "%.0f px") {
            set("drag_threshold_px", it.roundToInt())
        }
        Label(stringResource(R.string.middle_pinch))
        Chips(MiddlePinchAction.entries.map { stringResource(middleLabel(it)) }, middle.ordinal) {
            AppPrefs.setMiddlePinch(MiddlePinchAction.entries[it])
        }
        SliderRow(stringResource(R.string.scroll_speed), settings.float("scroll_gain"), 0.2f..5f, "%.1f") { set("scroll_gain", it) }
        SwitchRow(stringResource(R.string.natural_scroll), naturalScroll, AppPrefs::setNaturalScroll)

        Section(stringResource(R.string.section_camera))
        Chips(
            listOf(stringResource(R.string.camera_front), stringResource(R.string.camera_back)),
            selected = if (settings.int("camera_index") == 1) 1 else 0,
        ) { set("camera_index", it) }
        Label(stringResource(R.string.resolution))
        val resolutions = listOf(640 to 480, 1280 to 720)
        Chips(
            resolutions.map { (w, h) -> "$w × $h" },
            selected = resolutions.indexOf(settings.int("capture_width") to settings.int("capture_height")),
        ) { Engine.update(mapOf("capture_width" to "${resolutions[it].first}", "capture_height" to "${resolutions[it].second}")) }
        SwitchRow(stringResource(R.string.mirror), settings.flag("mirror")) { set("mirror", if (it) 1 else 0) }

        Section(stringResource(R.string.section_tracking))
        SliderRow(stringResource(R.string.hand_confidence), settings.float("hand_confidence"), 0.3f..0.95f, "%.2f") {
            set("hand_confidence", it)
        }
        SliderRow(
            stringResource(R.string.inference_threads), settings.int("inference_threads", 2).toFloat(), 1f..4f, "%.0f",
            steps = 2,
        ) { set("inference_threads", it.roundToInt()) }
        Hint(stringResource(R.string.applies_on_restart))
        SwitchRow(stringResource(R.string.idle_throttle), settings.flag("idle_throttle")) { set("idle_throttle", if (it) 1 else 0) }

        OutlinedButton(onClick = {
            Engine.resetSettings()
            AppPrefs.reset()
        }, modifier = Modifier.padding(top = 8.dp)) { Text(stringResource(R.string.reset_settings)) }
    }
}

@Composable
fun RecorderTab(model: DashboardModel, running: Boolean) {
    val snap = model.snapshot
    val labels = poseLabels()
    var label by rememberSaveable { mutableIntStateOf(Pose.PINCH_INDEX) }
    var delay by rememberSaveable { mutableFloatStateOf(3f) }
    var seconds by rememberSaveable { mutableFloatStateOf(6f) }
    val recording = snap?.recording == true

    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(stringResource(R.string.recorder_intro), style = MaterialTheme.typography.bodyMedium)
        Label(stringResource(R.string.recorder_gesture))
        Chips(labels.take(Pose.COUNT), label) { label = it }
        SliderRow(stringResource(R.string.recorder_delay), delay, 0f..10f, "%.0f s", steps = 9) { delay = it }
        SliderRow(stringResource(R.string.recorder_duration), seconds, 2f..30f, "%.0f s", steps = 27) { seconds = it }
        Button(
            enabled = running,
            onClick = {
                if (recording) NativeEngine.stopRecording()
                else NativeEngine.startRecording(label, delay.toDouble(), seconds.toDouble(), Engine.recordingsDir())
            },
        ) { Text(stringResource(if (recording) R.string.recorder_stop else R.string.recorder_start)) }
        when {
            !running -> Hint(stringResource(R.string.recorder_needs_tracking))
            snap != null && snap.recordingWaiting -> Text(stringResource(R.string.recorder_waiting, snap.recordingSecondsLeft), color = Warn)
            snap != null && recording ->
                Text(stringResource(R.string.recorder_recording, snap.recordingSecondsLeft, snap.recordedSamples), color = Bad)
        }
        if (model.recordingPath.isNotEmpty()) Hint(stringResource(R.string.recorder_saved, model.recordingPath))
    }
}

@Composable
private fun Section(title: String) {
    Text(title, style = MaterialTheme.typography.titleMedium, color = Accent, modifier = Modifier.padding(top = 8.dp))
}

@Composable
private fun Label(text: String, value: String? = null) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(text, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
        if (value != null) Text(value, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun Hint(text: String) {
    Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

@Composable
private fun SliderRow(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    format: String,
    steps: Int = 0,
    onChange: (Float) -> Unit,
) {
    Column {
        Label(label, format.format(value))
        Slider(value = value.coerceIn(range), onValueChange = onChange, valueRange = range, steps = steps)
    }
}

@Composable
private fun SwitchRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

@Composable
private fun Chips(labels: List<String>, selected: Int, enabled: (Int) -> Boolean = { true }, onSelect: (Int) -> Unit) {
    Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        labels.forEachIndexed { i, text ->
            FilterChip(selected = i == selected, onClick = { onSelect(i) }, label = { Text(text) }, enabled = enabled(i))
        }
    }
}
