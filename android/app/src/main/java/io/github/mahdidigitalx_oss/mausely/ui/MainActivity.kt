package io.github.mahdidigitalx_oss.mausely.ui

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import io.github.mahdidigitalx_oss.mausely.Engine

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Engine.init(this)
        enableEdgeToEdge()
        setContent {
            MauselyTheme {
                MainScreen()
            }
        }
    }
}
