package io.github.mahdidigitalx_oss.mausely.ui

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.PrimaryTabRow
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.LifecycleResumeEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import io.github.mahdidigitalx_oss.mausely.ControlService
import io.github.mahdidigitalx_oss.mausely.Engine
import io.github.mahdidigitalx_oss.mausely.NativeEngine
import io.github.mahdidigitalx_oss.mausely.R
import io.github.mahdidigitalx_oss.mausely.TrackingService

@Composable
fun MainScreen() {
    val context = LocalContext.current
    val running by Engine.running.collectAsStateWithLifecycle()
    val serviceActive by TrackingService.active.collectAsStateWithLifecycle()
    val error by Engine.error.collectAsStateWithLifecycle()
    val accessibilityOn by ControlService.connected.collectAsStateWithLifecycle()
    val settings by Engine.settings.collectAsStateWithLifecycle()
    val model = remember { DashboardModel() }

    var cameraGranted by remember { mutableStateOf(hasPermission(context, Manifest.permission.CAMERA)) }
    var cameraDenied by remember { mutableStateOf(false) }
    LifecycleResumeEffect(Unit) {
        cameraGranted = hasPermission(context, Manifest.permission.CAMERA)
        onPauseOrDispose { }
    }
    val notificationLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { }
    val startTracking = {
        if (Build.VERSION.SDK_INT >= 33 && !hasPermission(context, Manifest.permission.POST_NOTIFICATIONS)) {
            notificationLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)  // optional: the Pause / Stop buttons
        }
        TrackingService.start(context)
    }
    val cameraLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        cameraGranted = granted
        cameraDenied = !granted
        if (granted) startTracking()
    }

    // Poll the pipeline once per display frame while it runs (stops when the app is in the background).
    LaunchedEffect(running) {
        if (!running) {
            model.clearFrame()
            return@LaunchedEffect
        }
        while (true) {
            withFrameNanos { }
            model.poll()
        }
    }

    Scaffold { padding ->
        Column(
            Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Row(verticalAlignment = Alignment.Bottom) {
                Text(stringResource(R.string.app_name), style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.padding(start = 8.dp))
                Text(
                    stringResource(R.string.app_tagline),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(bottom = 4.dp),
                )
            }

            error?.let { MessageCard(stringResource(R.string.error_title), it, Bad) }
            if (!cameraGranted || !accessibilityOn) {
                SetupCard(
                    cameraGranted = cameraGranted,
                    cameraDenied = cameraDenied,
                    accessibilityOn = accessibilityOn,
                    onAllowCamera = { cameraLauncher.launch(Manifest.permission.CAMERA) },
                )
            }

            val snap = model.snapshot
            val enabled = snap?.enabled == true
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
                if (serviceActive) {
                    OutlinedButton(onClick = { TrackingService.stop(context) }) {
                        Text(stringResource(if (running) R.string.stop_tracking else R.string.starting))
                    }
                } else {
                    Button(onClick = {
                        if (cameraGranted) startTracking() else cameraLauncher.launch(Manifest.permission.CAMERA)
                    }) { Text(stringResource(R.string.start_tracking)) }
                }
                FilledTonalButton(
                    onClick = { NativeEngine.toggleEnabled() },
                    enabled = running,
                    colors = if (enabled) {
                        ButtonDefaults.filledTonalButtonColors(containerColor = Good.copy(alpha = 0.85f), contentColor = Color.White)
                    } else {
                        ButtonDefaults.filledTonalButtonColors()
                    },
                ) { Text(stringResource(if (enabled) R.string.control_on else R.string.control_paused)) }
            }
            Text(
                stringResource(R.string.control_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            CameraPanel(model, settings, running)

            var tab by rememberSaveable { mutableIntStateOf(0) }
            val tabs = listOf(R.string.tab_performance, R.string.tab_gestures, R.string.tab_settings, R.string.tab_recorder)
            PrimaryTabRow(selectedTabIndex = tab) {
                tabs.forEachIndexed { i, title ->
                    Tab(selected = tab == i, onClick = { tab = i }, text = { Text(stringResource(title), maxLines = 1) })
                }
            }
            when (tab) {
                0 -> PerformanceTab(model, settings)
                1 -> GesturesTab(model)
                2 -> SettingsTab(settings, model)
                else -> RecorderTab(model, running)
            }

            Spacer(Modifier.height(4.dp))
            Text(
                stringResource(R.string.privacy_note),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun SetupCard(cameraGranted: Boolean, cameraDenied: Boolean, accessibilityOn: Boolean, onAllowCamera: () -> Unit) {
    val context = LocalContext.current
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerHigh)) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            if (!cameraGranted) {
                SetupStep(
                    title = stringResource(R.string.setup_camera),
                    text = stringResource(if (cameraDenied) R.string.setup_camera_denied else R.string.setup_camera_desc),
                ) {
                    Button(onClick = onAllowCamera) { Text(stringResource(R.string.setup_allow)) }
                    if (cameraDenied) TextButton(onClick = { openAppInfo(context) }) { Text(stringResource(R.string.setup_app_info)) }
                }
            }
            if (!accessibilityOn) {
                SetupStep(
                    title = stringResource(R.string.setup_accessibility),
                    text = stringResource(R.string.setup_accessibility_desc),
                ) {
                    Button(onClick = {
                        context.startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                    }) { Text(stringResource(R.string.setup_open_settings)) }
                    TextButton(onClick = { openAppInfo(context) }) { Text(stringResource(R.string.setup_app_info)) }
                }
                if (Build.VERSION.SDK_INT >= 33) {
                    Text(
                        stringResource(R.string.setup_restricted),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }
    }
}

@Composable
private fun SetupStep(title: String, text: String, actions: @Composable () -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(title, style = MaterialTheme.typography.titleMedium)
        Text(text, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) { actions() }
    }
}

@Composable
private fun MessageCard(title: String, text: String, color: Color) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = color.copy(alpha = 0.18f)),
    ) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(title, style = MaterialTheme.typography.titleSmall, color = color)
            Text(text, style = MaterialTheme.typography.bodyMedium)
        }
    }
}

private fun hasPermission(context: Context, permission: String) =
    ContextCompat.checkSelfPermission(context, permission) == PackageManager.PERMISSION_GRANTED

private fun openAppInfo(context: Context) {
    context.startActivity(
        Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", context.packageName, null))
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
    )
}
