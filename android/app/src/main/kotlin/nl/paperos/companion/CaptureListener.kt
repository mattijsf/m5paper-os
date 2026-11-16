package nl.paperos.companion

import android.app.Notification
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification

/**
 * Keeps WhatsApp and Proton Mail notifications, drops everything else.
 * Never logs notification contents.
 */
class CaptureListener : NotificationListenerService() {

    override fun onListenerConnected() {
        Store.setConnected(this, true)
        // Pick up what is already in the shade.
        runCatching { activeNotifications?.forEach { capture(it) } }
    }

    override fun onListenerDisconnected() {
        Store.setConnected(this, false)
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        capture(sbn)
    }

    private fun capture(sbn: StatusBarNotification) {
        if (sbn.packageName !in Store.APPS) return
        val n = sbn.notification
        // Group summaries repeat the children ("3 new messages"); skip them.
        if (n.flags and Notification.FLAG_GROUP_SUMMARY != 0) return
        val e = n.extras
        val title = e.getCharSequence(Notification.EXTRA_TITLE)?.toString().orEmpty()
        val text = (e.getCharSequence(Notification.EXTRA_BIG_TEXT)
            ?: e.getCharSequence(Notification.EXTRA_TEXT))?.toString().orEmpty()
        val canReply = n.actions?.any { !it.remoteInputs.isNullOrEmpty() } == true
        Store.add(this, Item(sbn.packageName, sbn.key, sbn.postTime, title, text, canReply))
    }
}
