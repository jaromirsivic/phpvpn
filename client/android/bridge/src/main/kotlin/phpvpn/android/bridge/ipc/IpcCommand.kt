package phpvpn.android.bridge.ipc

/**
 * The closed command list of IPC v1: the original seven commands,
 * `get-server-info`, `dismiss-connect-diagnostic` and the seventeen
 * configuration and Wizard commands (core/ipc/ipc_protocol.cpp kCommandNames,
 * core/ipc/wizard_ipc.cpp names). [timeoutMs] is the tray budget of
 * ipc_timeouts.h for the request.
 */
enum class IpcCommand(val wireName: String, val wizardFamily: Boolean, val timeoutMs: Int) {
    GET_STATUS("get-status", false, IpcTimeouts.STATUS_REQUEST_MS),
    CONNECT("connect", false, IpcTimeouts.COMMAND_MS),
    DISCONNECT("disconnect", false, IpcTimeouts.COMMAND_MS),
    GET_INFO("get-info", false, IpcTimeouts.COMMAND_MS),
    GET_SERVER_INFO("get-server-info", false, IpcTimeouts.COMMAND_MS),
    GET_CONFIG("get-config", false, IpcTimeouts.COMMAND_MS),
    SET_CONFIG("set-config", false, IpcTimeouts.EDITOR_SAVE_DEADLINE_MS),
    GET_DIAGNOSTICS("get-diagnostics", false, IpcTimeouts.COMMAND_MS),
    DISMISS_CONNECT_DIAGNOSTIC("dismiss-connect-diagnostic", false, IpcTimeouts.COMMAND_MS),

    GET_CONFIG_STATE("get-config-state", true, IpcTimeouts.WIZARD_REQUEST_MS),
    GET_CONFIG_DOCUMENT("get-config-document", true, IpcTimeouts.WIZARD_REQUEST_MS),
    GET_DEFAULT_CONFIG("get-default-config", true, IpcTimeouts.EDITOR_RESET_MS),
    CREATE_DEFAULT_CONFIG("create-default-config", true, IpcTimeouts.WIZARD_REQUEST_MS),
    WIZARD_OPEN("wizard-open", true, IpcTimeouts.WIZARD_REQUEST_MS),
    WIZARD_RESET("wizard-reset", true, IpcTimeouts.WIZARD_REQUEST_MS),
    WIZARD_KEEPALIVE("wizard-keepalive", true, IpcTimeouts.WIZARD_REQUEST_MS),
    WIZARD_CHECK_URL("wizard-check-url", true, IpcTimeouts.WIZARD_REQUEST_MS),
    WIZARD_CHECK_SECRET("wizard-check-secret", true, IpcTimeouts.WIZARD_REQUEST_MS),
    WIZARD_CHECK_ACCESS("wizard-check-access", true, IpcTimeouts.WIZARD_REQUEST_MS),
    WIZARD_GET_OPERATION("wizard-get-operation", true, IpcTimeouts.WIZARD_REQUEST_MS),
    WIZARD_CANCEL_OPERATION("wizard-cancel-operation", true, IpcTimeouts.WIZARD_REQUEST_MS),
    WIZARD_CANCEL_ATTEMPT("wizard-cancel-attempt", true, IpcTimeouts.WIZARD_REQUEST_MS),
    WIZARD_RESUME("wizard-resume", true, IpcTimeouts.WIZARD_REQUEST_MS),
    WIZARD_FINISH("wizard-finish", true, IpcTimeouts.WIZARD_REQUEST_MS),
    WIZARD_GET_COMMIT("wizard-get-commit", true, IpcTimeouts.WIZARD_REQUEST_MS),
    WIZARD_CLOSE("wizard-close", true, IpcTimeouts.WIZARD_CLOSE_MS),
    ;

    companion object {
        private val byWire = entries.associateBy { it.wireName }

        fun fromWire(name: String?): IpcCommand? = name?.let { byWire[it] }
    }
}
