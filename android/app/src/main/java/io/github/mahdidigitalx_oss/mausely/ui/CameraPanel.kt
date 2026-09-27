package io.github.mahdidigitalx_oss.mausely.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.scale
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.mahdidigitalx_oss.mausely.Pose
import io.github.mahdidigitalx_oss.mausely.R
import io.github.mahdidigitalx_oss.mausely.Snapshot
import io.github.mahdidigitalx_oss.mausely.flag
import io.github.mahdidigitalx_oss.mausely.float
import kotlin.math.cos
import kotlin.math.sin

private val Bones = arrayOf(
    0 to 1, 1 to 2, 2 to 3, 3 to 4, 0 to 5, 5 to 6, 6 to 7, 7 to 8, 5 to 9, 9 to 10, 10 to 11,
    11 to 12, 9 to 13, 13 to 14, 14 to 15, 15 to 16, 13 to 17, 17 to 18, 18 to 19, 19 to 20, 0 to 17,
)
private val Tips = setOf(4, 8, 12, 16, 20)

/** The camera image as the AI sees it, with the tracked hand drawn on top (like the desktop dashboard). */
@Composable
fun CameraPanel(model: DashboardModel, settings: Map<String, String>, running: Boolean) {
    val snap = model.snapshot
    val frame = model.frame
    val aspect = if (snap != null && snap.frameWidth > 0) snap.frameWidth.toFloat() / snap.frameHeight else 3f / 4f
    val measurer = rememberTextMeasurer()
    val poseLabels = poseLabels()
    val badge = stringResource(if (snap?.enabled == true) R.string.badge_on else R.string.badge_paused)

    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Box(
            Modifier
                .fillMaxWidth(if (aspect < 1f) 0.8f else 1f)
                .align(Alignment.CenterHorizontally)
                .aspectRatio(aspect)
                .clip(RoundedCornerShape(12.dp))
                .background(Color(0xFF14161A)),
            contentAlignment = Alignment.Center,
        ) {
            if (frame == null || snap == null) {
                Text(
                    stringResource(if (running) R.string.camera_waiting else R.string.camera_idle),
                    color = Color(0xFF969BA5),
                    textAlign = TextAlign.Center,
                    modifier = Modifier.padding(24.dp),
                )
            } else {
                val mirror = settings.flag("mirror")
                Canvas(Modifier.fillMaxSize()) {
                    val fw = snap.frameWidth.toFloat()
                    val fh = snap.frameHeight.toFloat()
                    scale(if (mirror) -1f else 1f, 1f) {
                        drawImage(frame, dstSize = IntSize(size.width.toInt(), size.height.toInt()))
                    }
                    fun p(x: Float, y: Float): Offset {
                        val u = x / fw
                        return Offset((if (mirror) 1f - u else u) * size.width, y / fh * size.height)
                    }
                    // Active region, defined in mirrored (display) coordinates.
                    val x0 = settings.float("region_x0") * size.width
                    val y0 = settings.float("region_y0") * size.height
                    drawRect(
                        RegionYellow.copy(alpha = 0.6f), Offset(x0, y0),
                        Size(settings.float("region_x1") * size.width - x0, settings.float("region_y1") * size.height - y0),
                        style = Stroke(1.5.dp.toPx()),
                    )
                    if (snap.handValid) drawHand(snap, model, fh, ::p, measurer, poseLabels)
                    drawBadge(badge, snap.enabled, measurer)
                }
            }
        }
        if (snap?.handValid == true) {
            Text(
                stringResource(
                    R.string.hand_info,
                    stringResource(if (snap.rightHand) R.string.hand_right else R.string.hand_left),
                    snap.presence,
                    stringResource(if (snap.handTracked) R.string.hand_tracking else R.string.hand_detected),
                ),
                style = MaterialTheme.typography.bodySmall,
            )
        } else if (running) {
            Text(stringResource(R.string.no_hand), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Text(stringResource(R.string.camera_legend), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
fun poseLabels() = listOf(
    stringResource(R.string.pose_move),
    stringResource(R.string.pose_pinch_index),
    stringResource(R.string.pose_pinch_middle),
    stringResource(R.string.pose_scroll),
    stringResource(R.string.pose_fist),
    stringResource(R.string.pose_none),
)

private fun DrawScope.drawHand(
    snap: Snapshot,
    model: DashboardModel,
    frameHeight: Float,
    p: (Float, Float) -> Offset,
    measurer: TextMeasurer,
    poseLabels: List<String>,
) {
    // Tracking region.
    val c = cos(snap.roi(3))
    val s = sin(snap.roi(3))
    val half = snap.roi(2) / 2
    val quad = Path()
    listOf(-1f to -1f, 1f to -1f, 1f to 1f, -1f to 1f).forEachIndexed { i, (cx, cy) ->
        val lx = cx * half
        val ly = cy * half
        val q = p(snap.roi(0) + c * lx - s * ly, snap.roi(1) + s * lx + c * ly)
        if (i == 0) quad.moveTo(q.x, q.y) else quad.lineTo(q.x, q.y)
    }
    quad.close()
    drawPath(quad, Color.White.copy(alpha = 0.25f), style = Stroke(1.dp.toPx()))

    // Skeleton.
    for ((a, b) in Bones) {
        drawLine(
            Color(0xDC5AE68C), p(snap.landmarkX(a), snap.landmarkY(a)), p(snap.landmarkX(b), snap.landmarkY(b)),
            strokeWidth = 2.dp.toPx(),
        )
    }
    for (i in 0 until 21) {
        val tip = i in Tips
        drawCircle(
            if (tip) Color(0xFFFF5A5A) else Color(0xFFF0F0FF),
            radius = (if (tip) 4.5f else 3f).dp.toPx(),
            center = p(snap.landmarkX(i), snap.landmarkY(i)),
        )
    }

    // Control point: raw (grey ring) and smoothed (accent).
    model.stats.lastHand?.let { m ->
        drawCircle(Color(0xA0C8C8C8), 7.dp.toPx(), p(m.rawPointX * frameHeight, m.rawPointY * frameHeight), style = Stroke(1.5.dp.toPx()))
        drawCircle(Accent, 6.dp.toPx(), p(m.smoothPointX * frameHeight, m.smoothPointY * frameHeight))
    }

    // Pose label under the wrist, and the fist-toggle progress ring around it.
    val wrist = p(snap.landmarkX(0), snap.landmarkY(0))
    val label = measurer.measure(poseLabels[snap.pose.coerceIn(0, Pose.NONE)], TextStyle(color = Color.White, fontSize = 13.sp))
    val topLeft = Offset(wrist.x - label.size.width / 2f, wrist.y + 10.dp.toPx())
    drawRoundRect(
        Color(0xAA000000), topLeft - Offset(6.dp.toPx(), 3.dp.toPx()),
        Size(label.size.width + 12.dp.toPx(), label.size.height + 6.dp.toPx()), CornerRadius(4.dp.toPx()),
    )
    drawText(label, topLeft = topLeft)
    if (snap.fistProgress > 0f) {
        val r = 22.dp.toPx()
        drawArc(
            RegionYellow, -90f, 360f * snap.fistProgress, useCenter = false,
            topLeft = wrist - Offset(r, r), size = Size(2 * r, 2 * r), style = Stroke(4.dp.toPx()),
        )
    }
}

private fun DrawScope.drawBadge(text: String, enabled: Boolean, measurer: TextMeasurer) {
    val layout = measurer.measure(text, TextStyle(color = Color.White, fontSize = 12.sp))
    val origin = Offset(10.dp.toPx(), 10.dp.toPx())
    drawRoundRect(
        if (enabled) Color(0xDC289650) else Color(0xDC464850), origin,
        Size(layout.size.width + 16.dp.toPx(), layout.size.height + 8.dp.toPx()), CornerRadius(4.dp.toPx()),
    )
    drawText(layout, topLeft = origin + Offset(8.dp.toPx(), 4.dp.toPx()))
}
