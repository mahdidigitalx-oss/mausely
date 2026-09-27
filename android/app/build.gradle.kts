import java.net.URI
import java.security.MessageDigest

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "io.github.mahdidigitalx_oss.mausely"
    compileSdk = 37
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "io.github.mahdidigitalx_oss.mausely"
        minSdk = 26  // continued accessibility gestures (drag) need Android 8.0
        targetSdk = 36
        versionCode = 1
        versionName = "0.2.0"

    }

    // One APK per ABI: arm64-v8a for phones, x86_64 for emulators and Chromebooks.
    splits {
        abi {
            isEnable = true
            reset()
            include("arm64-v8a", "x86_64")
            isUniversalApk = false
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
    }

    packaging {
        jniLibs {
            // Only the ONNX Runtime C library is used (from C++), not its Java bindings.
            excludes += "**/libonnxruntime4j_jni.so"
            // Compressed: halves the download (ONNX Runtime alone is 33 MB uncompressed).
            useLegacyPackaging = true
        }
    }
}

// Hand models from OpenCV Zoo (MediaPipe, Apache-2.0), pinned exactly like
// cmake/Assets.cmake, plus Mausely's own models from /models. They are packaged
// as assets and copied to internal storage on first start.
abstract class PrepareModels : DefaultTask() {
    @get:Input
    abstract val downloads: MapProperty<String, String>  // file name -> "url sha256"

    @get:InputFiles
    @get:PathSensitive(PathSensitivity.NAME_ONLY)
    abstract val ownModels: ConfigurableFileCollection

    @get:Internal
    abstract val cacheDir: DirectoryProperty

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun run() {
        val out = outputDir.get().asFile.resolve("models")
        out.deleteRecursively()
        out.mkdirs()
        val cache = cacheDir.get().asFile.apply { mkdirs() }
        for ((name, spec) in downloads.get()) {
            val (url, sha256) = spec.split(" ")
            val cached = cache.resolve(name)
            if (!cached.isFile || sha256(cached.readBytes()) != sha256) {
                logger.lifecycle("Downloading $name")
                val bytes = URI(url).toURL().openStream().use { it.readBytes() }
                check(sha256(bytes) == sha256) { "SHA-256 mismatch for $url" }
                cached.writeBytes(bytes)
            }
            cached.copyTo(out.resolve(name), overwrite = true)
        }
        ownModels.forEach { it.copyTo(out.resolve(it.name), overwrite = true) }
    }

    private fun sha256(bytes: ByteArray) =
        MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
}

val openCvZoo = "https://media.githubusercontent.com/media/opencv/opencv_zoo/47534e27c9851bb1128ccc0102f1145e27f23f98/models"
val prepareModels = tasks.register<PrepareModels>("prepareModels") {
    downloads.put(
        "palm_detection_mediapipe_2023feb.onnx",
        "$openCvZoo/palm_detection_mediapipe/palm_detection_mediapipe_2023feb.onnx " +
            "78ff51c38496b7fc8b8ebdb6cc8c1abb02fa6c38427c6848254cdaba57fcce7c",
    )
    downloads.put(
        "handpose_estimation_mediapipe_2023feb.onnx",
        "$openCvZoo/handpose_estimation_mediapipe/handpose_estimation_mediapipe_2023feb.onnx " +
            "db0898ae717b76b075d9bf563af315b29562e11f8df5027a1ef07b02bef6d81c",
    )
    ownModels.from(fileTree(rootDir.parentFile.resolve("models")) { include("*.onnx") })
    cacheDir.set(rootDir.parentFile.resolve(".deps/models"))
    outputDir.set(layout.buildDirectory.dir("generated/models"))
}

androidComponents {
    onVariants { variant ->
        variant.sources.assets?.addGeneratedSourceDirectory(prepareModels, PrepareModels::outputDir)
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.service)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.camera.camera2)
    implementation(libs.androidx.camera.lifecycle)
    // Packages libonnxruntime.so, which the C++ core loads with dlopen().
    implementation(libs.onnxruntime.android)
    debugImplementation(libs.androidx.compose.ui.tooling)

    testImplementation(libs.junit)
}
