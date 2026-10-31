package phpvpn.android.ui.screens

import androidx.compose.runtime.Composable
import androidx.compose.runtime.ProvidableCompositionLocal
import androidx.compose.runtime.remember
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.platform.LocalContext
import phpvpn.android.appContainer
import phpvpn.android.bridge.controller.ContractsSaveMachineHandle
import phpvpn.android.bridge.controller.PasswordKeyModelHandle
import phpvpn.android.bridge.controller.SaveResponseClassifier
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.MonotonicClock
import phpvpn.android.bridge.repository.ConnectionRepository
import phpvpn.android.standin.StandInContractsSaveMachine
import phpvpn.android.standin.StandInPasswordKeyModel
import phpvpn.android.standin.StandInSaveResponseClassifier

/**
 * What the configuration flows of WP10 (contracts, editor, saved outcomes)
 * need. The defaults come from the process [phpvpn.android.AppContainer];
 * the native models are the Kotlin stand-ins until WP04 provides the JNI
 * handles, which then replace only the three factories here. UI tests
 * provide their own instance through [LocalConfigFlowDependencies].
 */
class ConfigFlowDependencies(
    val ipcClient: IpcClient,
    val connection: ConnectionRepository,
    val classifier: SaveResponseClassifier = StandInSaveResponseClassifier,
    val contractsMachine: () -> ContractsSaveMachineHandle = { StandInContractsSaveMachine() },
    val passwordKeyModel: () -> PasswordKeyModelHandle = { StandInPasswordKeyModel() },
    val clock: MonotonicClock = MonotonicClock.SYSTEM,
)

/** Override of the flow dependencies; null means the application container. */
val LocalConfigFlowDependencies: ProvidableCompositionLocal<ConfigFlowDependencies?> = staticCompositionLocalOf { null }

@Composable
fun configFlowDependencies(): ConfigFlowDependencies {
    LocalConfigFlowDependencies.current?.let { return it }
    val context = LocalContext.current
    return remember(context) {
        val container = context.appContainer
        ConfigFlowDependencies(container.ipcClient, container.connection)
    }
}
