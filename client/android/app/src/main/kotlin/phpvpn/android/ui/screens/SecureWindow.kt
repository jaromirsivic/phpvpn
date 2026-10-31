package phpvpn.android.ui.screens

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.view.Window
import android.view.WindowManager
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.ui.platform.LocalContext
import java.util.WeakHashMap

/**
 * FLAG_SECURE for the screens that show secrets (14, 15, 20, 21, 23 of
 * plan-android-v001, T-U01): no screenshots, no screen recording and a blank
 * thumbnail in Recents while at least one of them is composed. Screens
 * overlap during navigation transitions, so the flag is reference counted
 * per window and cleared only when the last secure screen leaves.
 */
object SecureWindowFlags {
    private val holders = WeakHashMap<Window, Int>()

    fun acquire(window: Window) = synchronized(holders) {
        val count = holders[window] ?: 0
        if (count == 0) window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        holders[window] = count + 1
    }

    fun release(window: Window) = synchronized(holders) {
        val count = (holders[window] ?: return@synchronized) - 1
        if (count <= 0) {
            holders.remove(window)
            window.clearFlags(WindowManager.LayoutParams.FLAG_SECURE)
        } else {
            holders[window] = count
        }
    }

    /** Number of secure screens currently holding [window] (for tests). */
    fun holders(window: Window): Int = synchronized(holders) { holders[window] ?: 0 }
}

/** Marks the hosting window secure for as long as the caller is in the composition. */
@Composable
fun SecureWindow() {
    val context = LocalContext.current
    DisposableEffect(context) {
        val window = context.findActivity()?.window
        window?.let(SecureWindowFlags::acquire)
        onDispose { window?.let(SecureWindowFlags::release) }
    }
}

internal tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}
