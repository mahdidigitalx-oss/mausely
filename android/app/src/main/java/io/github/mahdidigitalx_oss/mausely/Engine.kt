package io.github.mahdidigitalx_oss.mausely

import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.annotation.MainThread
import androidx.annotation.WorkerThread
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import java.io.File
import java.util.concurrent.Executors

/** Which camera to open and at what resolution (settings `camera_index`, `capture_*`). */
data class CameraConfig(val front: Boolean, val width: Int, val height: Int)

/**
 * Process-wide owner of the native pipeline and its settings. The UI edits
 * settings here; [TrackingService] starts and stops the pipeline.
 */
@SuppressLint("StaticFieldLeak")  // holds the application context only
object Engine {
    // Phones: the front camera at 640x480 is plenty for the 192/224 px models.
    private const val ANDROID_DEFAULTS = "camera_index=0\ncapture_width=640\ncapture_height=480\n"

    private lateinit var app: Context
    private var initError: String? = null
    private val io = Executors.newSingleThreadExecutor()

    private val _settings = MutableStateFlow<Map<String, String>>(emptyMap())
    val settings: StateFlow<Map<String, String>> = _settings.asStateFlow()

    private val _running = MutableStateFlow(false)
    val running: StateFlow<Boolean> = _running.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    /** Why the pipeline could not start (missing models, ONNX Runtime...), or null. */
    val error: StateFlow<String?> = _error.asStateFlow()

    val cameraConfig: Flow<CameraConfig>
        get() = settings.map {
            CameraConfig(it.int("camera_index") != 1, it.int("capture_width", 640), it.int("capture_height", 480))
        }.distinctUntilChanged()

    @MainThread
    fun init(context: Context) {
        if (::app.isInitialized) return
        app = context.applicationContext
        initError = NativeEngine.init(File(app.filesDir, "mausely.log").path)
        val file = settingsFile()
        val text = if (file.isFile) file.readText() else ANDROID_DEFAULTS
        _settings.value = SettingsText.parse(NativeEngine.normalizeSettings(text))
        AppPrefs.init(app)
    }

    /** Changes some settings; they apply to the running pipeline on its next frame and are saved. */
    fun update(changes: Map<String, String>) = applyText(SettingsText.format(_settings.value + changes))

    fun resetSettings() = applyText(ANDROID_DEFAULTS)

    private fun applyText(text: String) {
        val normalized = NativeEngine.normalizeSettings(text)
        _settings.value = SettingsText.parse(normalized)
        NativeEngine.updateSettings(normalized)
        io.execute { settingsFile().writeText(normalized) }
    }

    /** Loads the models and starts processing. Returns an error message or null. */
    @WorkerThread
    fun startPipeline(): String? {
        val err = initError ?: runCatching {
            NativeEngine.start(extractModels().path + "/", SettingsText.format(_settings.value))
        }.getOrElse { "Cannot prepare the models: ${it.message}" }
        _error.value = err
        _running.value = err == null
        return err
    }

    @WorkerThread
    fun stopPipeline() {
        NativeEngine.stop()
        _running.value = false
    }

    fun reportError(message: String) {
        _error.value = message
    }

    /** Folder for gesture recordings (Android/data/<app>/files/recordings/), with a trailing slash. */
    fun recordingsDir(): String = (app.getExternalFilesDir("recordings") ?: File(app.filesDir, "recordings"))
        .apply { mkdirs() }.path + "/"

    private fun settingsFile() = File(app.filesDir, "settings.ini")

    // ONNX Runtime reads models from files: copy them out of the APK once per install/update.
    private fun extractModels(): File {
        val dir = File(app.noBackupFilesDir, "models")
        val stamp = File(dir, ".installed")
        val version = installTime().toString()
        if (stamp.isFile && stamp.readText() == version) return dir
        dir.mkdirs()
        for (name in app.assets.list("models").orEmpty()) {
            app.assets.open("models/$name").use { input -> File(dir, name).outputStream().use { input.copyTo(it) } }
        }
        stamp.writeText(version)
        return dir
    }

    private fun installTime(): Long {
        val pm = app.packageManager
        val info = if (Build.VERSION.SDK_INT >= 33) {
            pm.getPackageInfo(app.packageName, PackageManager.PackageInfoFlags.of(0))
        } else {
            @Suppress("DEPRECATION")
            pm.getPackageInfo(app.packageName, 0)
        }
        return info.lastUpdateTime
    }
}
