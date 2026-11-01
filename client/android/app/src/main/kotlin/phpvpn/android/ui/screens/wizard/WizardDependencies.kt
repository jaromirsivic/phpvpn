package phpvpn.android.ui.screens.wizard

import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.CoroutineScope
import phpvpn.android.appContainer
import phpvpn.android.bridge.ipc.IdSource
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.MonotonicClock
import phpvpn.android.bridge.repository.ConnectionRepository

/**
 * What the Wizard and the HTTP diagnostic views need from the process: the
 * IPC client (each Wizard session creates its own WizardController on it),
 * a [backgroundScope] that outlives the Wizard for the best-effort
 * wizard-close and for an explicit Connect from 17, and the
 * [connection] repository whose status carries the Connect diagnostic (30,
 * 31). In the app everything comes from the AppContainer; UI tests provide a
 * scripted transport through [LocalWizardDependencies].
 */
class WizardDependencies(
    val client: IpcClient,
    val backgroundScope: CoroutineScope,
    val connection: ConnectionRepository?,
    val clock: MonotonicClock = MonotonicClock.SYSTEM,
    val ids: IdSource = IdSource.RANDOM,
)

/** Override of the process dependencies, for tests only; null in the app. */
val LocalWizardDependencies = staticCompositionLocalOf<WizardDependencies?> { null }

/** The dependencies of this composition: the override, else the AppContainer. */
@Composable
fun rememberWizardDependencies(): WizardDependencies {
    LocalWizardDependencies.current?.let { return it }
    val context = LocalContext.current
    return remember(context) {
        val container = context.appContainer
        WizardDependencies(container.ipcClient, container.scope, container.connection)
    }
}
