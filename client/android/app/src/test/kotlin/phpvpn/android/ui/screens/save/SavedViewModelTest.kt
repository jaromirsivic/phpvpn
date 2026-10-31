package phpvpn.android.ui.screens.save

import kotlinx.coroutines.test.advanceUntilIdle
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.debug.ConfigReplies
import phpvpn.android.debug.ConfigReplies.answer
import phpvpn.android.debug.ConfigReplies.answerNext
import phpvpn.android.ui.screens.runFlowTest

/**
 * Screen 25 (tray `requestReconnectAfterSave` and ReconnectController): one
 * status read after the confirmed save, Reconnect only for a connected VPN
 * with the Disconnect completed before the single Connect, Connect for an
 * inactive VPN, nothing automatic when the status is unknown.
 */
class SavedViewModelTest {
    private fun FakeIpcTransport.commands() = requests.map { it.command }

    @Test
    fun connectedOffersReconnectAndConnectWaitsForTheCompletedDisconnect() {
        val transport = FakeIpcTransport()
        // Initial read, revalidation before Disconnect, two reads while stopping, idle, final check.
        transport.answerNext(
            "get-status",
            ConfigReplies.CONNECTED,
            ConfigReplies.CONNECTED,
            ConfigReplies.STOPPING,
            ConfigReplies.DISCONNECTED_SAME,
            ConfigReplies.DISCONNECTED_SAME,
        )
        transport.answer("disconnect", ConfigReplies.STOPPING)
        transport.answer("connect", ConfigReplies.status("connecting", "health", epoch = 4, started = false))
        runFlowTest(transport) { deps ->
            val vm = SavedViewModel(deps.connection, deps.clock)
            advanceUntilIdle()
            assertEquals(SavedConnection.ACTIVE, vm.connectionState)
            assertEquals(listOf("get-status"), transport.commands())
            vm.reconnect()
            advanceUntilIdle()
            assertTrue(vm.finished)
            assertEquals(ReconnectPhase.IDLE, vm.phase)
            val commands = transport.commands()
            assertEquals(1, commands.count { it == "disconnect" })
            assertEquals(1, commands.count { it == "connect" })
            // Connect only after the Disconnect and the confirmed idle status of the same connection.
            assertTrue(commands.indexOf("disconnect") < commands.indexOf("connect"))
            assertEquals("get-status", commands[commands.indexOf("connect") - 1])
        }
    }

    @Test
    fun aDisconnectThatNeverCompletesNeverConnects() {
        val transport = FakeIpcTransport()
        transport.answerNext("get-status", ConfigReplies.CONNECTED, ConfigReplies.CONNECTED)
        transport.answer("disconnect", ConfigReplies.STOPPING)
        runFlowTest(transport) { deps ->
            // Every later read is still stopping, until the 30 s deadline.
            transport.answerNext("get-status", *Array(80) { ConfigReplies.STOPPING })
            val vm = SavedViewModel(deps.connection, deps.clock)
            advanceUntilIdle()
            vm.reconnect()
            advanceUntilIdle()
            assertEquals(ReconnectPhase.FAILED, vm.phase)
            assertFalse(vm.finished)
            assertEquals(0, transport.requests("connect").size)
            assertTrue(testScheduler.currentTime >= SavedViewModel.DISCONNECT_DEADLINE_MS)
        }
    }

    @Test
    fun aDifferentConnectionAfterConsentCancelsTheReconnect() {
        val transport = FakeIpcTransport()
        transport.answerNext("get-status", ConfigReplies.CONNECTED, ConfigReplies.status("connected", "running", epoch = 9))
        runFlowTest(transport) { deps ->
            val vm = SavedViewModel(deps.connection, deps.clock)
            advanceUntilIdle()
            vm.reconnect()
            advanceUntilIdle()
            assertEquals(ReconnectPhase.FAILED, vm.phase)
            assertEquals(0, transport.requests("disconnect").size)
            assertEquals(0, transport.requests("connect").size)
        }
    }

    @Test
    fun inactiveOffersConnectAndUnknownStatusOffersNothingAutomatic() {
        val transport = FakeIpcTransport()
        transport.answer("get-status", ConfigReplies.DISCONNECTED)
        transport.answer("connect", ConfigReplies.status("connecting", "health", epoch = 4, started = false))
        runFlowTest(transport) { deps ->
            val vm = SavedViewModel(deps.connection, deps.clock)
            advanceUntilIdle()
            assertEquals(SavedConnection.INACTIVE, vm.connectionState)
            vm.reconnect() // not offered
            vm.connect()
            advanceUntilIdle()
            assertTrue(vm.finished)
            assertEquals(1, transport.requests("connect").size)
            assertEquals(0, transport.requests("disconnect").size)
        }
        val silent = FakeIpcTransport()
        runFlowTest(silent) { deps ->
            val vm = SavedViewModel(deps.connection, deps.clock)
            advanceUntilIdle()
            assertEquals(SavedConnection.UNAVAILABLE, vm.connectionState)
            vm.reconnect()
            vm.connect()
            advanceUntilIdle()
            assertEquals(listOf("get-status"), silent.requests.map { it.command })
        }
        val busy = FakeIpcTransport()
        busy.answer("get-status", ConfigReplies.status("connecting", "health", epoch = 4, started = false))
        runFlowTest(busy) { deps ->
            val vm = SavedViewModel(deps.connection, deps.clock)
            advanceUntilIdle()
            assertEquals(SavedConnection.BUSY, vm.connectionState)
        }
    }
}
