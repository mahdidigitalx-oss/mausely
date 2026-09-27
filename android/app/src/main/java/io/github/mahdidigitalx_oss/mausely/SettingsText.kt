package io.github.mahdidigitalx_oss.mausely

/**
 * The pipeline settings as `key=value` lines (the desktop settings.ini format).
 * The C++ side owns the keys, defaults and validation; see pipeline/settings.cpp.
 */
object SettingsText {
    fun parse(text: String): Map<String, String> =
        text.lineSequence()
            .map { it.trim() }
            .filter { it.isNotEmpty() && !it.startsWith("#") && !it.startsWith(";") && '=' in it }
            .associate { it.substringBefore('=').trim() to it.substringAfter('=').trim() }

    fun format(values: Map<String, String>): String =
        values.entries.joinToString("\n", postfix = "\n") { (k, v) -> "$k=$v" }
}

/** Typed access to a settings map. Missing or malformed values read as the given default. */
fun Map<String, String>.float(key: String, default: Float = 0f) = get(key)?.toFloatOrNull() ?: default
fun Map<String, String>.int(key: String, default: Int = 0) = get(key)?.toDoubleOrNull()?.toInt() ?: default
fun Map<String, String>.flag(key: String) = get(key) == "1" || get(key) == "true"
