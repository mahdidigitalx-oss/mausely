package io.github.mahdidigitalx_oss.mausely.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// The desktop dashboard's palette.
val Accent = Color(0xFF3D9EF5)
val Good = Color(0xFF4CCC73)
val Warn = Color(0xFFF2B340)
val Bad = Color(0xFFF25959)
val RegionYellow = Color(0xFFFFC83C)

private val Dark = darkColorScheme(
    primary = Accent,
    onPrimary = Color.White,
    secondary = Color(0xFF3FD6C6),
    background = Color(0xFF121418),
    surface = Color(0xFF121418),
    surfaceContainer = Color(0xFF1B1E24),
    surfaceContainerHigh = Color(0xFF22262D),
    onSurfaceVariant = Color(0xFF9AA0AB),
)

private val Light = lightColorScheme(
    primary = Color(0xFF1F78D1),
    secondary = Color(0xFF14A091),
)

@Composable
fun MauselyTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = if (isSystemInDarkTheme()) Dark else Light, content = content)
}
