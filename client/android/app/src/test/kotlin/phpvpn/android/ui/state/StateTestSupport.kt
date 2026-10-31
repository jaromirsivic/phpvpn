@file:OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)

package phpvpn.android.ui.state

import android.content.Intent
import androidx.activity.result.ActivityResultLauncher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain
import org.junit.rules.TestWatcher
import org.junit.runner.Description
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.bridge.ipc.IdSource
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.MonotonicClock
import phpvpn.android.bridge.repository.ConnectionRepository

/** viewModelScope runs on Dispatchers.Main; tests share one virtual-time scheduler. */
@OptIn(ExperimentalCoroutinesApi::class)
class MainDispatcherRule(val dispatcher: TestDispatcher = StandardTestDispatcher()) : TestWatcher() {
    override fun starting(description: Description) = Dispatchers.setMain(dispatcher)

    override fun finished(description: Description) = Dispatchers.resetMain()
}

/** Deterministic UUIDv4 request ids (…4000-8000-0000000000NN). */
class SequentialIds(private var next: Int = 100) : IdSource {
    override fun next(): String = "00000000-0000-4000-8000-%012x".format(next++)
}

fun TestScope.virtualClock() = MonotonicClock { testScheduler.currentTime }

fun TestScope.client(transport: FakeIpcTransport) = IpcClient(transport, SequentialIds(), clock = virtualClock())

fun TestScope.connectionRepository(client: IpcClient) = ConnectionRepository(client, backgroundScope, virtualClock())

/** Consent fake for JVM tests: no system dialog exists here. */
class FakeConsent(var granted: Boolean) : VpnConsent {
    var requests = 0

    override fun isGranted(): Boolean = granted

    override fun request(launcher: ActivityResultLauncher<Intent>): Boolean {
        requests++
        return !granted
    }
}
