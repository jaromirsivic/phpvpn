package phpvpn.android.ui.screens.secure

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.view.Window
import android.view.WindowManager
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.platform.LocalView
import phpvpn.android.R

/**
 * Screens that show a secret (14, 15 and the header editor 13 here; 20, 21
 * and 23 in WP10) call [SecureWindow] once. While at least one such screen is
 * composed, the activity window has `FLAG_SECURE`: no screenshots, no screen
 * recording and a blank thumbnail in Recents (plan-android-v001, R5).
 *
 * The flag is reference counted on the window, so a navigation transition
 * that briefly composes two secret screens never clears it in between, and
 * a flag the window already had before is left untouched.
 */
@Composable
fun SecureWindow() {
    if (!LocalSecureWindowEnabled.current) return
    val view = LocalView.current
    DisposableEffect(view) {
        val window = view.context.findActivity()?.window
        if (window != null) SecureWindowFlags.acquire(window)
        onDispose { if (window != null) SecureWindowFlags.release(window) }
    }
}

/**
 * True in the app. Only the screenshot tests of the design comparison turn it
 * off, because FLAG_SECURE makes captures of 14 and 15 black; the
 * instrumented tests of the flag itself keep it on.
 */
val LocalSecureWindowEnabled = staticCompositionLocalOf { true }

/** The reference count behind [SecureWindow], kept as tags on the decor view. */
object SecureWindowFlags {
    fun acquire(window: Window) {
        val decor = window.decorView
        val count = (decor.getTag(R.id.secure_window_count) as? Int ?: 0) + 1
        decor.setTag(R.id.secure_window_count, count)
        if (count == 1) {
            val had = isSecure(window)
            decor.setTag(R.id.secure_window_previous, had)
            if (!had) window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        }
    }

    fun release(window: Window) {
        val decor = window.decorView
        val count = (decor.getTag(R.id.secure_window_count) as? Int ?: 1) - 1
        decor.setTag(R.id.secure_window_count, count)
        if (count <= 0 && decor.getTag(R.id.secure_window_previous) != true) {
            window.clearFlags(WindowManager.LayoutParams.FLAG_SECURE)
        }
    }

    /** True while the window carries FLAG_SECURE. */
    fun isSecure(window: Window): Boolean = (window.attributes.flags and WindowManager.LayoutParams.FLAG_SECURE) != 0
}

private tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}
