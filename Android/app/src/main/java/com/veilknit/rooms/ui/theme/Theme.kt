package com.veilknit.rooms.ui.theme

import android.app.Activity
import android.os.Build
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat

private val Scheme = darkColorScheme(
    primary = VeilRed,
    onPrimary = VeilText,
    primaryContainer = VeilRedDark,
    onPrimaryContainer = VeilText,
    secondary = VeilMuted,
    onSecondary = VeilWindow,
    background = VeilWindow,
    onBackground = VeilText,
    surface = VeilPanel,
    onSurface = VeilText,
    surfaceVariant = VeilEdit,
    onSurfaceVariant = VeilMuted,
    outline = VeilBorder,
    error = VeilError,
)

@Composable
fun VeilKnitRoomsTheme(content: @Composable () -> Unit) {
    val view = LocalView.current
    if (!view.isInEditMode) {
        SideEffect {
            val window = (view.context as Activity).window
            window.statusBarColor = VeilWindow.toArgb()
            window.navigationBarColor = VeilWindow.toArgb()
            WindowCompat.getInsetsController(window, view).apply {
                isAppearanceLightStatusBars = false
                isAppearanceLightNavigationBars = false
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                window.isStatusBarContrastEnforced = false
                window.isNavigationBarContrastEnforced = false
            }
        }
    }
    MaterialTheme(colorScheme = Scheme, content = content)
}
