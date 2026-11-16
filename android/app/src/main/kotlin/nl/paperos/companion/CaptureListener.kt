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
        // Pick up what is already in the shade, and forget what went away while disconnected.
        runCatching {
            val active = activeNotifications.orEmpty()
            Store.retainOnly(this, active.filter { keep(it) }.map { it.key }.toSet())
            active.forEach { capture(it) }
        }
    }

    override fun onListenerDisconnected() {
        Store.setConnected(this, false)
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        capture(sbn)
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        if (keep(sbn)) Store.remove(this, sbn.key)
    }

    // Group summaries repeat the children ("3 new messages"); skip them.
    private fun keep(sbn: StatusBarNotification) =
        sbn.packageName in Store.APPS && (sbn.notification.flags and Notification.FLAG_GROUP_SUMMARY) == 0

    private fun capture(sbn: StatusBarNotification) {
        if (!keep(sbn)) return
        val n = sbn.notification
        val e = n.extras
        val title = e.getCharSequence(Notification.EXTRA_TITLE)?.toString().orEmpty()
        val text = (e.getCharSequence(Notification.EXTRA_BIG_TEXT)
            ?: e.getCharSequence(Notification.EXTRA_TEXT))?.toString().orEmpty()
        val canReply = n.actions?.any { !it.remoteInputs.isNullOrEmpty() } == true
        Store.add(this, Item(sbn.packageName, sbn.key, sbn.postTime, title, text, canReply))
    }
}
