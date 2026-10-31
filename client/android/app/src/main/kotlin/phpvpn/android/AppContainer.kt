package phpvpn.android

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import phpvpn.android.bridge.ipc.ConfigDocumentCheck
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.IpcTransport
import phpvpn.android.bridge.ipc.StructuralConfigDocumentCheck
import phpvpn.android.bridge.repository.ConnectionRepository
import phpvpn.android.bridge.repository.ServerInfoHook
import phpvpn.android.bridge.repository.ServerInfoRepository
import phpvpn.android.bridge.repository.StatisticsRepository

/**
 * Process-wide dependencies of the UI (plan-android-v001, "Kotlin část"). One
 * instance lives in [PhpvpnApplication]; screens reach it through
 * [appContainer]. The [transport] is the platform-specific piece: the JNI
 * transport of WP04 in the real app, a fake in UI tests and previews.
 * [documentCheck] is the native configuration validator in the app (WP04) and
 * the structural approximation of WP06 in JVM tests. [serverInfoHook] is the
 * native `ServerInfoModel` in the app (`NativeServerInfoHook`, whose `latest`
 * the Server screens of WP08 read) and accept-all in tests.
 * Flow controllers (Wizard, editor, contracts) are created per flow by the
 * screens of WP09 and WP10 from [ipcClient]; they are not process-wide.
 */
class AppContainer(
    val transport: IpcTransport,
    documentCheck: ConfigDocumentCheck = StructuralConfigDocumentCheck,
    val serverInfoHook: ServerInfoHook = ServerInfoHook.ACCEPT_ALL,
) {
    /** Background scope of the repositories, alive as long as the process. */
    val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    val ipcClient: IpcClient = IpcClient(transport, documentCheck = documentCheck)
    val connection: ConnectionRepository = ConnectionRepository(ipcClient, scope)
    val statistics: StatisticsRepository = StatisticsRepository(connection, scope)
    val serverInfo: ServerInfoRepository = ServerInfoRepository(ipcClient, scope, serverInfoHook)
}
