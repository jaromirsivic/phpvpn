package phpvpn.android.bridge

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.VpnService
import android.os.IBinder
import android.os.SystemClock
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.uiautomator.By
import androidx.test.uiautomator.UiDevice
import androidx.test.uiautomator.Until
import java.io.File
import java.net.NetworkInterface
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * T-A01 and T-A01b (plan-android-v001 WP03) on the emulator, through the real
 * C++ path: the production AndroidNetworkHost and JNI bridge, AndroidVpnContext,
 * AndroidNetConfig and AndroidTunnelDriver from the platform factories, driven
 * by the core's TunnelSession, with [TestVpnService] as the TunnelHost.
 *
 * Procedure: the test obtains the VPN consent (system dialog accepted with
 * UiAutomator), runs the T-A01b checks as uid 2000 without the VPN, starts the
 * session (10.99.99.2/24, route 10.99.99.0/24, MTU 1500, no DNS), checks the
 * VPN network through ConnectivityManager, starts the native responder, pings
 * 10.99.99.3 as uid 2000 (the echo requests must arrive through FdTunnel and
 * the replies written back must reach ping), repeats the T-A01b checks with the
 * VPN up, stops the responder (nothing outside the prefix may have entered the
 * tunnel) and stops the session (the VPN network must vanish within 2 s).
 * The HTTPS check needs the WP01 tls_probe pushed to
 * /data/local/tmp/phpvpn-tls-probe (see wp03-platform-report); without it the
 * check reports "missing". Every result is logged with the tag phpvpn-vpn-test.
 */
@RunWith(AndroidJUnit4::class)
class VpnPlatformTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context: Context = instrumentation.targetContext

    @Test
    fun virtualInterfaceCarriesOnlyTheVpnPrefix() {
        log("api ${android.os.Build.VERSION.SDK_INT}, package ${context.packageName}")
        ensureVpnConsent()
        val (service, connection) = bindTestService()
        try {
            val before = otherUidChecks("before")
            assertEquals("the checks must run as the adb shell user", "2000", before["uid"])
            assertEquals("baseline DNS of uid 2000", "ok", before["dns"])
            assertEquals("baseline LAN of uid 2000", "ok", before["lan"])

            val directory = File(context.noBackupFilesDir, "vpn-test").apply { deleteRecursively(); mkdirs() }
            val started = VpnTestLibrary.text(
                VpnTestLibrary.vpnStart(
                    AndroidNetworkHost(context), service, context.packageName.toByteArray(), directory.absolutePath.toByteArray(),
                ),
            )
            log("vpnStart: $started")
            assertTrue("vpnStart: $started", started.startsWith("ok"))
            log("spec: ${service.lastSpecJson}")
            assertEquals(1, service.establishCalls)
            try {
                verifyVpnNetwork()

                assertTrue(VpnTestLibrary.text(VpnTestLibrary.vpnResponderStart()).startsWith("ok"))
                // The per-uid VPN routing of netd can trail the VPN network by
                // a moment (API 36 lost the first echo once): wait for the first
                // reply, then measure. "ready" counts the unanswered probes.
                val ping = ShellUser.run(
                    """
                    i=0
                    while [ §i -lt 20 ]; do
                      if ping -c 1 -W 1 10.99.99.3 >/dev/null 2>&1; then break; fi
                      i=§((i + 1))
                    done
                    echo "ready=§i"
                    ping -c 3 -W 2 10.99.99.3
                    """.trimIndent().replace('§', '$'),
                )
                log("ping 10.99.99.3 as uid 2000:\n$ping")
                val after = otherUidChecks("after")
                val summary = VpnTestLibrary.text(VpnTestLibrary.vpnResponderStop())
                log("responder: $summary")
                val counters = summary.split(' ').mapNotNull { part ->
                    part.split('=', limit = 2).takeIf { it.size == 2 }?.let { it[0] to it[1] }
                }.toMap()

                // T-A01: read and write through FdTunnel.
                assertTrue("ping must get replies through the tunnel:\n$ping", ping.contains(" 3 received"))
                assertTrue("echo requests: $summary", (counters["echoRequests"]?.toLong() ?: 0) >= 4)
                assertEquals("every echo request answered: $summary", counters["echoRequests"], counters["echoReplies"])
                assertTrue("targets: $summary", counters["echoTargets"].orEmpty().contains("10.99.99.3"))
                // T-A01b: nothing but the prefix enters the tunnel, DNS included.
                assertEquals("packets outside the prefix: $summary", "0", counters["outsidePrefix"])
                // Only the kernel's link-local or multicast housekeeping of tun0 (ipv6Local).
                assertEquals("IPv6 packets to global addresses in the tunnel: $summary", "0", counters["ipv6"])
                // T-A01b: what worked without the VPN still works with it.
                for (key in CHECK_KEYS) {
                    if (before[key] == "ok") assertEquals("$key of uid 2000 with the VPN up", "ok", after[key])
                }
            } finally {
                val begin = SystemClock.elapsedRealtime()
                val stopped = VpnTestLibrary.text(VpnTestLibrary.vpnStop())
                var gone = VpnInterfaces.ownVpnNetworks(context).isEmpty()
                while (!gone && SystemClock.elapsedRealtime() - begin < 5_000) {
                    SystemClock.sleep(20)
                    gone = VpnInterfaces.ownVpnNetworks(context).isEmpty()
                }
                val elapsed = SystemClock.elapsedRealtime() - begin
                log("vpnStop: $stopped, VPN network gone after $elapsed ms: $gone")
                assertTrue("vpnStop: $stopped", stopped.startsWith("ok"))
                assertTrue("journal: $stopped", stopped.contains("journalRemoved=true"))
                assertTrue("the VPN network must vanish", gone)
                assertTrue("the VPN network must vanish within 2 s, took $elapsed ms", elapsed <= 2_000)
            }
            val recovered = otherUidChecks("released")
            for (key in CHECK_KEYS) {
                if (before[key] == "ok") assertEquals("$key of uid 2000 after the release", "ok", recovered[key])
            }
        } finally {
            context.unbindService(connection)
        }
        assertTrue("the consent stays granted", VpnService.prepare(context) == null)
    }

    /**
     * T-A13 on the emulator: an underlying network itself uses the VPN prefix.
     * Prepared outside the test, because only root can add the address
     * (google_apis images): `adb root`, `adb shell ip addr add 10.99.99.17/24
     * dev wlan0`; IpClient then reports it in the LinkProperties of Wi-Fi.
     * Skipped when no underlying address lies inside 10.99.99.0/24.
     */
    @Test
    fun preflightRefusesAnUnderlyingAddressInsideTheVpnPrefix() {
        val snapshot = AndroidNetworkHost(context).snapshotNetworks().toString(Charsets.UTF_8)
        assumeTrue(
            "no underlying address inside 10.99.99.0/24 (T-A13 needs adb root and ip addr add)",
            snapshot.contains("\"address\":\"10.99.99."),
        )
        log("T-A13 snapshot: $snapshot")
        ensureVpnConsent()
        val (service, connection) = bindTestService()
        try {
            val directory = File(context.noBackupFilesDir, "vpn-preflight").apply { deleteRecursively(); mkdirs() }
            val started = VpnTestLibrary.text(
                VpnTestLibrary.vpnStart(
                    AndroidNetworkHost(context), service, context.packageName.toByteArray(), directory.absolutePath.toByteArray(),
                ),
            )
            log("T-A13 vpnStart: $started")
            assertTrue(started, started.startsWith("error netconfig_preflight_conflict"))
            assertEquals("nothing may be established", 0, service.establishCalls)
            assertTrue(VpnInterfaces.ownVpnNetworks(context).isEmpty())
        } finally {
            context.unbindService(connection)
        }
    }

    @Test
    fun missingVpnServiceIsADefinedError() {
        val result = VpnTestLibrary.text(
            VpnTestLibrary.vpnLoadCheck(AndroidNetworkHost(context), context.packageName.toByteArray()),
        )
        log("loadAndVerify without TunnelHost: $result")
        // Without the consent loadAndVerify stops earlier with tunnel_permission_missing.
        if (VpnService.prepare(context) == null) {
            assertEquals("error tunnel_adapter_failed (the VPN service is not running)", result)
        } else {
            assertEquals("error tunnel_permission_missing (the VPN consent is missing)", result)
        }
    }

    @Test
    fun startupRecoveryRetiresTheJournalOfADeadProcess() {
        val directory = File(context.noBackupFilesDir, "vpn-recovery").apply { deleteRecursively(); mkdirs() }
        val result = VpnTestLibrary.text(
            VpnTestLibrary.vpnRecoveryCheck(
                AndroidNetworkHost(context), context.packageName.toByteArray(), directory.absolutePath.toByteArray(),
            ),
        )
        log("recoverPreviousRun with a stale journal: $result")
        assertEquals("ok journalRemoved=true", result)
        directory.deleteRecursively()
    }

    @Test
    fun hostNativesRegisterInTheProductionLibrary() {
        // JNI_OnLoad of libphpvpn-android.so registered VpnHosts next to NativeBridge.
        assertTrue(NativeBridge.version().isNotEmpty())
        VpnHosts.registerNetworkHost(AndroidNetworkHost(context))
        VpnHosts.registerTunnelHost(object : TunnelHost {
            override fun establish(specJson: ByteArray) = -1
            override fun lastEstablishError() = TunnelHost.ESTABLISH_FAILED
            override fun waitVpnNetworkGone(timeoutMs: Int) = true
        })
        VpnHosts.unregisterTunnelHost()
        val snapshot = AndroidNetworkHost(context).snapshotNetworks().toString(Charsets.UTF_8)
        log("network snapshot: $snapshot")
        assertTrue(snapshot.startsWith("{\"version\":1,"))
    }

    private fun ensureVpnConsent() {
        val request = VpnService.prepare(context)
        if (request == null) {
            log("the VPN consent is already granted")
            return
        }
        log("requesting the VPN consent: ${request.component}")
        val device = UiDevice.getInstance(instrumentation)
        device.wakeUp()
        ShellUser.run("wm dismiss-keyguard")
        // ConfirmDialog needs a caller started for result, see ConsentActivity.
        context.startActivity(Intent(context, ConsentActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        val accept = device.wait(Until.findObject(By.res("android", "button1")), 15_000)
        assertNotNull("the VPN consent dialog did not appear", accept)
        log("accepting the VPN consent dialog: ${accept.text}")
        accept.click()
        val deadline = SystemClock.elapsedRealtime() + 10_000
        while (VpnService.prepare(context) != null) {
            assertTrue("the VPN consent was not granted", SystemClock.elapsedRealtime() < deadline)
            SystemClock.sleep(100)
        }
    }

    private fun bindTestService(): Pair<TestVpnService, ServiceConnection> {
        val ready = CountDownLatch(1)
        var bound: TestVpnService? = null
        val connection = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
                bound = (binder as TestVpnService.LocalBinder).service
                ready.countDown()
            }

            override fun onServiceDisconnected(name: ComponentName?) = Unit
        }
        val intent = Intent(context, TestVpnService::class.java).setAction(TestVpnService.ACTION_TEST_BIND)
        assertTrue("the test VpnService cannot be bound", context.bindService(intent, connection, Context.BIND_AUTO_CREATE))
        assertTrue("the test VpnService did not connect", ready.await(10, TimeUnit.SECONDS))
        return bound!! to connection
    }

    // T-A01: the VPN network has exactly the configured address, the route of
    // the prefix, MTU 1500 and no DNS server.
    private fun verifyVpnNetwork() {
        val connectivity = context.getSystemService(ConnectivityManager::class.java)
        val deadline = SystemClock.elapsedRealtime() + 5_000
        var networks = VpnInterfaces.ownVpnNetworks(context)
        while (networks.isEmpty() && SystemClock.elapsedRealtime() < deadline) {
            SystemClock.sleep(20)
            networks = VpnInterfaces.ownVpnNetworks(context)
        }
        assertEquals("exactly one VPN network of this app", 1, networks.size)
        val network = networks.single()
        val properties = connectivity.getLinkProperties(network)
        val capabilities = connectivity.getNetworkCapabilities(network)
        assertNotNull(properties)
        assertNotNull(capabilities)
        properties!!
        capabilities!!
        log("VPN network: interface ${properties.interfaceName}, addresses ${properties.linkAddresses}, " +
            "routes ${properties.routes}, dns ${properties.dnsServers}, mtu ${properties.mtu}, " +
            "notMetered ${capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED)}, " +
            "internet ${capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)}")
        assertEquals(listOf("10.99.99.2/24"), properties.linkAddresses.map { it.toString() })
        val ipv4Routes = properties.routes.filter { it.destination.address is java.net.Inet4Address }
        assertEquals(listOf("10.99.99.0/24"), ipv4Routes.map { it.destination.toString() })
        assertTrue("no IPv4 default route", ipv4Routes.none { it.isDefaultRoute })
        assertTrue("no DNS server", properties.dnsServers.isEmpty())
        assertEquals(1500, properties.mtu)
        val name = properties.interfaceName
        assertNotNull(name)
        val nic = NetworkInterface.getByName(name)
        log("kernel interface $name: index ${nic?.index}, mtu ${nic?.mtu}, up ${nic?.isUp}")
        if (nic != null) assertEquals(1500, nic.mtu)
        // The app itself stays on the underlying network (point 5).
        val active = connectivity.getNetworkCapabilities(connectivity.activeNetwork)
        assertTrue("the app is not covered by its own VPN", active?.hasTransport(NetworkCapabilities.TRANSPORT_VPN) == false)
    }

    // T-A01b as uid 2000: DNS (cached and fresh name), TCP 443, HTTPS with a
    // verified certificate, IPv6 to the emulator router, LAN ping and TCP
    // to the emulator host (10.0.2.2).
    private fun otherUidChecks(phase: String): Map<String, String> {
        val fresh = "wp03-${System.currentTimeMillis()}-$phase.1-2-3-4.sslip.io"
        val script = CHECK_SCRIPT.replace("FRESH", fresh).replace('§', '$')
        val output = ShellUser.run(script)
        log("checks $phase:\n$output")
        return ShellUser.values(output)
    }

    private fun log(message: String) = Log.i(TAG, message)

    private companion object {
        const val TAG = "phpvpn-vpn-test"
        val CHECK_KEYS = listOf("dns", "dnsfresh", "tcp443", "https", "ipv6", "lan", "lantcp")
        val CHECK_SCRIPT = """
            echo "uid=§(id -u)"
            r=§(ping -c 1 -W 2 www.google.com 2>&1 | head -1)
            case "§r" in *"PING www.google.com ("*) echo "dns=ok";; *) echo "dns=fail §r";; esac
            f=§(ping -c 1 -W 2 FRESH 2>&1 | head -1)
            case "§f" in *"(1.2.3.4)"*) echo "dnsfresh=ok";; *) echo "dnsfresh=fail §f";; esac
            # toybox 0.8.9 (API 34) has no nc -z; an empty line works everywhere.
            if echo | nc -w 5 www.google.com 443 >/dev/null 2>&1; then echo "tcp443=ok"; else echo "tcp443=fail"; fi
            if [ -x /data/local/tmp/phpvpn-tls-probe ]; then
              t=§(/data/local/tmp/phpvpn-tls-probe https://www.google.com/ 2>&1 | grep 'verify=system-store' | head -1)
              case "§t" in *"curl=0 "*) echo "https=ok";; *) echo "https=fail §t";; esac
            else
              echo "https=missing"
            fi
            if ping6 -c 1 -W 2 fec0::2 >/dev/null 2>&1; then echo "ipv6=ok"; else echo "ipv6=fail"; fi
            if ping -c 1 -W 2 10.0.2.2 >/dev/null 2>&1; then echo "lan=ok"; else echo "lan=fail"; fi
            # TCP to the host behind 10.0.2.2: the adb server (5037) always listens during the test.
            if echo | nc -w 3 10.0.2.2 5037 >/dev/null 2>&1; then echo "lantcp=ok"; else echo "lantcp=fail"; fi
        """.trimIndent()
    }
}
