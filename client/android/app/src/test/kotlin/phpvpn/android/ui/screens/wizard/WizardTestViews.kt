package phpvpn.android.ui.screens.wizard

import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.HttpHeaders
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.ipc.WizardContext
import phpvpn.android.bridge.ipc.WizardFields
import phpvpn.android.bridge.ipc.WizardMode
import phpvpn.android.bridge.ipc.WizardView

/** Views of the desktop tests (`modelView` of test_wizard_model.cpp). */
object WizardTestViews {
    const val ID = "12345678-1234-4234-8234-123456789abc"
    const val OPID = "12345678-1234-4234-8234-123456789abd"

    /** `makeNewWizardDocument()` with the URL of the desktop test. */
    fun modelView(
        sessionId: String = ID,
        secret: String = WizardRules.DEFAULT_SERVER_SECRET,
        useDefaultSecret: Boolean = false,
        virtualIp: String = "",
        accessKey: String = "",
        useDefaultKey: Boolean = true,
        autoDetectIp: Boolean = true,
        mode: WizardMode = WizardMode.CREATE,
        contractCount: ULong = 0uL,
        vpnCidr: String = "10.99.99.0/24",
        epoch: ULong = 0uL,
        peers: List<String> = emptyList(),
    ): WizardView = WizardView(
        sessionId = sessionId,
        serviceInstanceId = OPID,
        revisionToken = null,
        availability = ConfigAvailability.MISSING,
        mode = mode,
        fields = WizardFields(
            serverUrl = "http://localhost/phpvpn.php",
            serverSecret = SecretText.of(secret),
            virtualIp = virtualIp,
            accessKey = SecretText.of(accessKey),
            httpHeaders = HttpHeaders.DEFAULT,
            useDefaultServerSecretInWizard = useDefaultSecret,
            autoDetectIpAddressInWizard = autoDetectIp,
            useDefaultAccessKeyInWizard = useDefaultKey,
        ),
        context = WizardContext(
            httpHeaders = HttpHeaders.DEFAULT,
            vpnCidr = vpnCidr,
            trustAnyTlsCertificate = true,
            maxOutboundQueueBytes = 134_217_728,
            maxInboundQueueBytes = 134_217_728,
            minOutboundPerPeerQueueBytes = 262_144,
            minInboundPerPeerQueueBytes = 262_144,
            maxQueuedPacketAgeMs = 5_000,
            maxResponseBodyBytes = 67_108_864,
            maxNdjsonLineBytes = 67_108_864,
            maxDataRecordsPerResponse = 65_535,
            responseTimeoutSeconds = 30,
            timeSyncIntervalSeconds = 300,
        ),
        contractCount = contractCount,
        existingDefaultContractPeers = peers,
        connectionEpoch = epoch,
        vpnState = ConnectionState.DISCONNECTED,
        vpnPhase = ConnectionPhase.IDLE,
    )

    fun key(secret: SecretText): String = secret.reveal()
}
