package phpvpn.android

import android.app.Application
import android.content.Context
import androidx.annotation.VisibleForTesting
import kotlinx.coroutines.cancel
import phpvpn.android.bridge.NativeRuntime
import phpvpn.android.bridge.ipc.IpcTransport
import phpvpn.android.bridge.ipc.ServerInfoResult
import phpvpn.android.bridge.jni.JniIpcTransport
import phpvpn.android.bridge.jni.NativeConfigDocumentCheck
import phpvpn.android.bridge.jni.NativeServerInfo
import phpvpn.android.bridge.jni.ServerInfoParse
import phpvpn.android.ui.state.ServerClientState
import phpvpn.android.ui.state.ServerInfoModel
import phpvpn.android.ui.state.ServerInfoModelResult
import phpvpn.android.ui.state.ServerInfoModelSource
import phpvpn.android.ui.state.ServerInfoRow
import phpvpn.android.ui.state.SystemVpnConsent

/**
 * Owns the [AppContainer]. The native runtime starts with the first
 * transport, once per process (plan-android-v001, "Proces a životnost"); the
 * service and the boot receiver of WP05 call [NativeRuntime.ensureStarted] as
 * well, which then returns at once. The container talks to the runtime through
 * the in-process JNI transport, checks configuration documents with the native
 * validator and classifies Server Info with the native model. If the start
 * failed, every exchange returns no reply and the UI shows the "status
 * unavailable" states (08, 27, 45), never a success.
 *
 * Open so that UI tests can install a test application with a scripted
 * transport (androidTest `PhpvpnTestRunner`); such an application overrides
 * [createTransport] and never starts the native runtime.
 */
open class PhpvpnApplication : Application() {
    lateinit var container: AppContainer
        private set

    override fun onCreate() {
        super.onCreate()
        container = createContainer()
    }

    /** WP05 adds the service-backed Always-on status and wraps Connect here. */
    protected open fun createContainer(): AppContainer = AppContainer(
        createTransport(),
        SystemVpnConsent(this),
        serverInfoModel = NativeServerInfoModel,
        documentCheck = NativeConfigDocumentCheck,
    )

    /** The JNI transport of the native runtime, started here on first use. */
    protected open fun createTransport(): IpcTransport {
        NativeRuntime.ensureStarted(this)
        return JniIpcTransport()
    }

    /** UI tests: drop every repository and flag and start from a fresh container. */
    @VisibleForTesting
    fun resetContainer() {
        container.scope.cancel()
        container = createContainer()
    }
}

/**
 * The WP08 [ServerInfoModelSource] over the native `buildServerInfoModel`
 * (core/presentation/server_info_model.cpp) instead of its Kotlin mirror.
 */
private object NativeServerInfoModel : ServerInfoModelSource {
    override fun build(result: ServerInfoResult): ServerInfoModelResult = when (val parsed = NativeServerInfo.parse(result)) {
        is ServerInfoParse.Failed -> ServerInfoModelResult.Invalid(parsed.code.wireName)
        is ServerInfoParse.Parsed -> ServerInfoModelResult.Ok(
            ServerInfoModel(
                infoPlaintext = parsed.view.infoPlaintext,
                localAddress = parsed.view.localAddress,
                rows = parsed.view.rows.map { row ->
                    ServerInfoRow(row.peer, row.contract, row.lastSeenMs, ServerClientState.entries[row.state.ordinal])
                },
                onlineClients = parsed.view.onlineClients,
                validOnlineClients = parsed.view.validOnlineClients,
            ),
        )
    }
}

/** The process-wide container; valid for any Context of this application. */
val Context.appContainer: AppContainer
    get() = (applicationContext as PhpvpnApplication).container
