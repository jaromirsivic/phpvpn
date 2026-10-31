package phpvpn.android

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.IpcTransport
import phpvpn.android.bridge.repository.ConnectionRepository
import phpvpn.android.bridge.repository.ServerInfoHook
import phpvpn.android.bridge.repository.ServerInfoRepository
import phpvpn.android.bridge.repository.StatisticsRepository
import phpvpn.android.ui.state.AlwaysOnStatus
import phpvpn.android.ui.state.ConfigSummaryRepository
import phpvpn.android.ui.state.ConnectActions
import phpvpn.android.ui.state.ConnectionUiEvents
import phpvpn.android.ui.state.KotlinServerInfoModel
import phpvpn.android.ui.state.RepositoryConnectActions
import phpvpn.android.ui.state.ServerInfoModelResult
import phpvpn.android.ui.state.ServerInfoModelSource
import phpvpn.android.ui.state.VpnConsent

/**
 * Process-wide dependencies of the UI (plan-android-v001, "Kotlin část"). One
 * instance lives in [PhpvpnApplication]; screens reach it through
 * [appContainer]. The [transport] is the only platform-specific piece: the
 * JNI transport of WP04 in the real app, a fake in UI tests and previews.
 * Flow controllers (Wizard, editor, contracts) are created per flow by the
 * screens of WP09 and WP10 from [ipcClient]; they are not process-wide.
 *
 * The seams for later work packages: WP04 passes the JNI transport and the
 * native [serverInfoModel]; WP05 passes [alwaysOn], wraps [connectActions]
 * (foreground service before Connect) and reports into [connectionEvents].
 */
class AppContainer(
    val transport: IpcTransport,
    val vpnConsent: VpnConsent,
    val alwaysOn: AlwaysOnStatus = AlwaysOnStatus.UNKNOWN,
    val serverInfoModel: ServerInfoModelSource = KotlinServerInfoModel,
    connectActions: ((ConnectionRepository) -> ConnectActions)? = null,
) {
    /** Background scope of the repositories, alive as long as the process. */
    val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    val ipcClient: IpcClient = IpcClient(transport)
    val connection: ConnectionRepository = ConnectionRepository(ipcClient, scope)
    val statistics: StatisticsRepository = StatisticsRepository(connection, scope)

    // A snapshot the model cannot verify is a failed refresh (45), exactly like
    // buildServerInfoModel failing in server_info_window.cpp.
    val serverInfo: ServerInfoRepository = ServerInfoRepository(
        ipcClient,
        scope,
        ServerInfoHook { reply -> serverInfoModel.build(reply.result) is ServerInfoModelResult.Ok },
    )
    val configSummary: ConfigSummaryRepository = ConfigSummaryRepository(ipcClient)
    val connectionEvents: ConnectionUiEvents = ConnectionUiEvents()
    val connectActions: ConnectActions = connectActions?.invoke(connection) ?: RepositoryConnectActions(connection)
}
