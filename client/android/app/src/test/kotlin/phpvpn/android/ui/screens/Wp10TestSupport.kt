package phpvpn.android.ui.screens

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.bridge.ipc.IdSource
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.MonotonicClock
import phpvpn.android.bridge.repository.ConnectionRepository

/** Deterministic UUIDv4 request identifiers. */
class SequentialIds(private var next: Int = 100) : IdSource {
    override fun next(): String = "00000000-0000-4000-8000-%012x".format(next++)
}

/**
 * Runs a ViewModel test on virtual time: Dispatchers.Main (viewModelScope) is
 * the test scheduler, the monotonic clock follows it, and [block] gets
 * dependencies over [transport].
 */
@OptIn(ExperimentalCoroutinesApi::class)
fun runFlowTest(transport: FakeIpcTransport = FakeIpcTransport(), block: suspend TestScope.(ConfigFlowDependencies) -> Unit) = runTest {
    Dispatchers.setMain(StandardTestDispatcher(testScheduler))
    try {
        val clock = MonotonicClock { testScheduler.currentTime }
        val client = IpcClient(transport, SequentialIds(), clock = clock)
        val deps = ConfigFlowDependencies(client, ConnectionRepository(client, backgroundScope, clock), clock = clock)
        block(deps)
    } finally {
        Dispatchers.resetMain()
    }
}

/** Configuration writes a test must account for. */
fun FakeIpcTransport.writes(): Int = requests("set-config").size + requests("create-default-config").size
