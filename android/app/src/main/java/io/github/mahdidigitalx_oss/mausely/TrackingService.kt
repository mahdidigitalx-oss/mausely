package io.github.mahdidigitalx_oss.mausely

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.Point
import android.hardware.display.DisplayManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Size
import android.view.Display
import android.view.ViewConfiguration
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleService
import androidx.lifecycle.lifecycleScope
import io.github.mahdidigitalx_oss.mausely.ui.MainActivity
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import kotlin.math.max
import kotlin.math.min

/**
 * Foreground service that owns the camera and the pipeline, so hand tracking
 * keeps running while other apps are in front. Frames go to the C++ pipeline;
 * its mouse actions reach [ControlService].
 */
class TrackingService : LifecycleService() {
    // Runs pipeline start/stop and the camera analyzer, in that order.
    private val worker: ExecutorService = Executors.newSingleThreadExecutor { Thread(it, "mausely-camera") }
    private var analysis: ImageAnalysis? = null
    private lateinit var displays: DisplayManager

    private val displayListener = object : DisplayManager.DisplayListener {
        override fun onDisplayAdded(displayId: Int) = Unit
        override fun onDisplayRemoved(displayId: Int) = Unit
        override fun onDisplayChanged(displayId: Int) {
            if (displayId == Display.DEFAULT_DISPLAY) updateDisplay()
        }
    }

    override fun onCreate() {
        super.onCreate()
        Engine.init(this)
        _active.value = true
        createChannel()
        displays = getSystemService(DisplayManager::class.java)
        try {
            ServiceCompat.startForeground(
                this, NOTIFICATION_ID, notification(enabled = false),
                if (Build.VERSION.SDK_INT >= 30) ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA else 0,
            )
        } catch (e: RuntimeException) {
            // The camera permission was revoked, or the app is no longer in front.
            Engine.reportError(getString(R.string.error_camera, e.message))
            stopSelf()
            return
        }
        displays.registerDisplayListener(displayListener, Handler(Looper.getMainLooper()))
        updateDisplay()

        worker.execute {
            val error = Engine.startPipeline()
            ContextCompat.getMainExecutor(this).execute {
                if (!lifecycle.currentState.isAtLeast(Lifecycle.State.CREATED)) return@execute
                if (error != null) stopSelf() else lifecycleScope.launch { bindCamera(Engine.cameraConfig.first()) }
            }
        }
        lifecycleScope.launch {
            Engine.cameraConfig.drop(1).collect { if (analysis != null) bindCamera(it) }
        }
        lifecycleScope.launch {
            // Keep the notification's Pause / Resume in step with fist toggles.
            val status = IntArray(5)
            var shown = false
            while (isActive) {
                NativeEngine.status(status)
                val enabled = status[0] == 1
                if (enabled != shown) {
                    shown = enabled
                    getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID, notification(enabled))
                }
                delay(300)
            }
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        super.onStartCommand(intent, flags, startId)
        when (intent?.action) {
            ACTION_TOGGLE -> NativeEngine.toggleEnabled()
            ACTION_STOP -> stopSelf()
        }
        return START_NOT_STICKY  // the camera cannot be reopened from the background anyway
    }

    override fun onDestroy() {
        displays.unregisterDisplayListener(displayListener)
        analysis?.clearAnalyzer()
        worker.execute { Engine.stopPipeline() }  // releases any held touch first
        worker.shutdown()
        _active.value = false
        super.onDestroy()
    }

    private fun bindCamera(config: CameraConfig) {
        val future = ProcessCameraProvider.getInstance(this)
        future.addListener({
            if (!lifecycle.currentState.isAtLeast(Lifecycle.State.CREATED)) return@addListener
            val provider = future.get()
            // Resolution in sensor orientation (landscape); the frame is rotated upright in C++.
            val size = Size(max(config.width, config.height), min(config.width, config.height))
            val wide = size.width * 3 > size.height * 4 + 1
            val selector = ResolutionSelector.Builder()
                .setAspectRatioStrategy(
                    if (wide) AspectRatioStrategy.RATIO_16_9_FALLBACK_AUTO_STRATEGY
                    else AspectRatioStrategy.RATIO_4_3_FALLBACK_AUTO_STRATEGY,
                )
                .setResolutionStrategy(ResolutionStrategy(size, ResolutionStrategy.FALLBACK_RULE_CLOSEST_HIGHER_THEN_LOWER))
                .build()
            val useCase = ImageAnalysis.Builder()
                .setResolutionSelector(selector)
                .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_RGBA_8888)
                .setTargetRotation(displayRotation())
                .build()
            useCase.setAnalyzer(worker) { image ->
                image.use {
                    val plane = it.planes[0]
                    NativeEngine.submitFrame(plane.buffer, it.width, it.height, plane.rowStride, it.imageInfo.rotationDegrees)
                }
            }
            val camera = when {
                config.front && provider.hasCamera(CameraSelector.DEFAULT_FRONT_CAMERA) -> CameraSelector.DEFAULT_FRONT_CAMERA
                provider.hasCamera(CameraSelector.DEFAULT_BACK_CAMERA) -> CameraSelector.DEFAULT_BACK_CAMERA
                else -> CameraSelector.DEFAULT_FRONT_CAMERA
            }
            provider.unbindAll()
            runCatching { provider.bindToLifecycle(this, camera, useCase) }
                .onSuccess { analysis = useCase }
                .onFailure {
                    Engine.reportError(getString(R.string.error_camera, it.message))
                    stopSelf()
                }
        }, ContextCompat.getMainExecutor(this))
    }

    private fun displayRotation() = displays.getDisplay(Display.DEFAULT_DISPLAY)?.rotation ?: 0

    // The active region maps onto the whole display; the camera image follows its rotation.
    private fun updateDisplay() {
        val display = displays.getDisplay(Display.DEFAULT_DISPLAY) ?: return
        val size = Point()
        @Suppress("DEPRECATION")
        display.getRealSize(size)
        NativeEngine.setScreen(size.x, size.y, ViewConfiguration.getDoubleTapTimeout())
        analysis?.targetRotation = display.rotation
    }

    private fun createChannel() {
        val channel = NotificationChannel(CHANNEL_ID, getString(R.string.notification_channel), NotificationManager.IMPORTANCE_LOW)
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    private fun notification(enabled: Boolean): Notification {
        fun action(action: String) = PendingIntent.getService(
            this, action.hashCode(), Intent(this, TrackingService::class.java).setAction(action),
            PendingIntent.FLAG_IMMUTABLE,
        )
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(getString(R.string.notification_title))
            .setContentText(getString(if (enabled) R.string.notification_on else R.string.notification_paused))
            .setContentIntent(open)
            .setOngoing(true)
            .setSilent(true)
            .addAction(0, getString(if (enabled) R.string.action_pause else R.string.action_resume), action(ACTION_TOGGLE))
            .addAction(0, getString(R.string.action_stop), action(ACTION_STOP))
            .setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE)
            .build()
    }

    companion object {
        private const val CHANNEL_ID = "tracking"
        private const val NOTIFICATION_ID = 1
        private const val ACTION_TOGGLE = "io.github.mahdidigitalx_oss.mausely.TOGGLE"
        private const val ACTION_STOP = "io.github.mahdidigitalx_oss.mausely.STOP"

        private val _active = MutableStateFlow(false)
        /** The service is running (the pipeline itself reports through [Engine.running]). */
        val active: StateFlow<Boolean> = _active.asStateFlow()

        /** Call from the foreground (an Activity): the camera is only granted to services started there. */
        fun start(context: Context) {
            ContextCompat.startForegroundService(context, Intent(context, TrackingService::class.java))
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, TrackingService::class.java))
        }
    }
}
