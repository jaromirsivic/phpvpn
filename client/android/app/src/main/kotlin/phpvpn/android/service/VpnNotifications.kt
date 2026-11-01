package phpvpn.android.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import phpvpn.android.MainActivity
import phpvpn.android.R

/**
 * The background notification (09): channel "VPN connection" with low
 * importance, a monochrome state icon, the state as title, server and virtual
 * IP as text, Disconnect only for connected, connecting and reconnecting, and
 * a tap that opens the app. Without POST_NOTIFICATIONS Android hides it; the
 * foreground service runs anyway (plan step 5).
 */
class VpnNotifications(private val context: Context) {
    private val manager = context.getSystemService(NotificationManager::class.java)

    fun createChannel() {
        val channel = NotificationChannel(CHANNEL_ID, context.getString(R.string.notification_channel_name), NotificationManager.IMPORTANCE_LOW)
        channel.description = context.getString(R.string.notification_channel_description)
        channel.setShowBadge(false)
        manager.createNotificationChannel(channel)
    }

    fun build(model: NotificationModel, text: String?): Notification {
        val builder = Notification.Builder(context, CHANNEL_ID)
            .setSmallIcon(iconOf(model.icon))
            .setContentTitle(context.getString(titleOf(model.kind)))
            .setContentIntent(openApp(context))
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setShowWhen(false)
            .setCategory(Notification.CATEGORY_SERVICE)
            .setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE)
            .setVisibility(Notification.VISIBILITY_PRIVATE)
        if (text != null) builder.setContentText(text)
        if (model.disconnectAction) {
            val disconnect = PendingIntent.getService(
                context,
                REQUEST_DISCONNECT,
                Intent(context, PhpvpnVpnService::class.java).setAction(PhpvpnVpnService.ACTION_DISCONNECT),
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
            )
            builder.addAction(Notification.Action.Builder(null, context.getString(R.string.notification_action_disconnect), disconnect).build())
        }
        return builder.build()
    }

    fun notify(notification: Notification) = manager.notify(NOTIFICATION_ID, notification)

    fun cancel() = manager.cancel(NOTIFICATION_ID)

    /** The server host and the virtual IP, or whichever of them is known; never a secret. */
    fun text(serverHost: String?, virtualIp: String?): String? = when {
        serverHost != null && virtualIp != null -> context.getString(R.string.notification_text, serverHost, virtualIp)
        else -> serverHost ?: virtualIp
    }

    companion object {
        const val CHANNEL_ID = "vpn-connection"
        const val NOTIFICATION_ID = 9
        private const val REQUEST_OPEN = 1
        private const val REQUEST_DISCONNECT = 2

        /**
         * Opens the app like its launcher icon: a new task starts on Connection,
         * an open task comes back unchanged, so an unsaved editor is never lost.
         * Also the configure intent of the VPN interface (plan step 2).
         */
        fun openApp(context: Context): PendingIntent = PendingIntent.getActivity(
            context,
            REQUEST_OPEN,
            Intent(context, MainActivity::class.java)
                .setAction(Intent.ACTION_MAIN)
                .addCategory(Intent.CATEGORY_LAUNCHER)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )

        fun titleOf(kind: NotificationKind): Int = when (kind) {
            NotificationKind.CONNECTING -> R.string.notification_title_connecting
            NotificationKind.CONNECTED -> R.string.notification_title_connected
            NotificationKind.RECONNECTING -> R.string.notification_title_reconnecting
            NotificationKind.DISCONNECTING -> R.string.notification_title_disconnecting
            NotificationKind.FAILED -> R.string.notification_title_failed
            NotificationKind.UNAVAILABLE -> R.string.notification_title_unavailable
        }

        fun iconOf(icon: NotificationIcon): Int = when (icon) {
            NotificationIcon.CONNECTED -> R.drawable.ic_notification_connected
            NotificationIcon.CONNECTING -> R.drawable.ic_notification_connecting
            NotificationIcon.DISCONNECTED -> R.drawable.ic_notification_disconnected
        }
    }
}
