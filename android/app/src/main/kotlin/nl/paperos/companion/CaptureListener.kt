package nl.paperos.companion

import android.app.Notification
import android.app.Person
import android.app.RemoteInput
import android.content.Intent
import android.os.Bundle
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import nl.paperos.companion.proto.Messages

/**
 * Keeps WhatsApp and Proton Mail notifications, drops everything else. While paired, each post,
 * update and removal goes to the Paper as an encrypted envelope; replies from the Paper are sent
 * through the notification's RemoteInput action. Never logs notification contents.
 */
class CaptureListener : NotificationListenerService() {

    override fun onListenerConnected() {
        instance = this
        Store.setConnected(this, true)
        // Pick up what is already in the shade, and forget what went away while disconnected.
        runCatching {
            val active = activeNotifications.orEmpty().filter { keep(it) }
            Store.retainOnly(this, active.map { it.key }.toSet())
            active.forEach { Store.add(this, meta(it)) }
            Link.sendSync(this, force = true)
        }
        RelayService.kick(this)
    }

    override fun onListenerDisconnected() {
        if (instance === this) instance = null
        Store.setConnected(this, false)
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        if (!keep(sbn)) return
        Store.add(this, meta(sbn))
        runCatching { Link.send(this, post(sbn)) }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification, rankingMap: RankingMap?, reason: Int) {
        if (!keep(sbn)) return
        Store.remove(this, sbn.key)
        val why = when (reason) {
            REASON_CLICK, REASON_APP_CANCEL, REASON_APP_CANCEL_ALL -> "read"
            REASON_CANCEL, REASON_CANCEL_ALL -> "dismissed"
            else -> "other"
        }
        runCatching { Link.send(this, Messages.remove(sbn.key, why)) }
    }

    // ---- replies (called from the relay thread)

    private fun find(wireKey: String): StatusBarNotification? =
        activeNotifications.orEmpty().firstOrNull { keep(it) && Messages.wireKey(it.key) == wireKey }

    private fun replyAction(sbn: StatusBarNotification): Notification.Action? =
        sbn.notification.actions?.firstOrNull { !it.remoteInputs.isNullOrEmpty() }

    fun hasReplyAction(wireKey: String): Boolean = runCatching { find(wireKey)?.let { replyAction(it) } != null }.getOrDefault(false)

    /** Sends [text] through the RemoteInput action. True when the PendingIntent was sent. */
    fun sendReply(wireKey: String, text: String): Boolean = runCatching {
        val sbn = find(wireKey) ?: return false
        val action = replyAction(sbn) ?: return false
        val inputs = action.remoteInputs
        val results = Bundle().apply { inputs.forEach { putCharSequence(it.resultKey, text) } }
        val intent = Intent().addFlags(Intent.FLAG_RECEIVER_FOREGROUND)
        RemoteInput.addResultsToIntent(inputs, intent, results)
        action.actionIntent.send(this, 0, intent)
        true
    }.getOrDefault(false)

    // ---- building messages

    private fun meta(sbn: StatusBarNotification) =
        Item(sbn.packageName, sbn.key, sbn.postTime, replyAction(sbn) != null)

    private fun post(sbn: StatusBarNotification): ByteArray? {
        val n = sbn.notification
        val e = n.extras
        val reply = replyAction(sbn) != null
        val ts = sbn.postTime / 1000
        val title = e.getCharSequence(Notification.EXTRA_TITLE)?.toString().orEmpty()
        val text = e.getCharSequence(Notification.EXTRA_TEXT)?.toString().orEmpty()
        return if (sbn.packageName.startsWith("com.whatsapp")) {
            val group = e.getBoolean(Notification.EXTRA_IS_GROUP_CONVERSATION)
            val chat = if (group) (e.getCharSequence(Notification.EXTRA_CONVERSATION_TITLE)?.toString() ?: title) else ""
            val self = e.getCharSequence(Notification.EXTRA_SELF_DISPLAY_NAME)?.toString() ?: "You"
            val msgs = e.getParcelableArray(Notification.EXTRA_MESSAGES, Bundle::class.java).orEmpty().mapNotNull { b ->
                val body = b.getCharSequence("text")?.toString() ?: return@mapNotNull null
                val from = b.getParcelable("sender_person", Person::class.java)?.name?.toString()
                    ?: b.getCharSequence("sender")?.toString()
                    ?: self
                Messages.Msg(from, body, b.getLong("time") / 1000)
            }.ifEmpty { listOf(Messages.Msg(title, text, ts)) }
            Messages.post(sbn.key, "whatsapp", chat, reply, ts, msgs)
        } else {
            // Proton Mail: title = sender, text = subject; a big text, when shown, is the preview.
            val big = e.getCharSequence(Notification.EXTRA_BIG_TEXT)?.toString()
            val preview = if (big != null && big != text) big.removePrefix(text).trim() else ""
            Messages.post(sbn.key, "mail", "", reply, ts, listOf(Messages.Msg(title, preview, ts, subj = text)))
        }
    }

    companion object {
        @Volatile var instance: CaptureListener? = null
            private set

        // Group summaries repeat the children ("3 new messages"); skip them.
        fun keep(sbn: StatusBarNotification) =
            sbn.packageName in Store.APPS && (sbn.notification.flags and Notification.FLAG_GROUP_SUMMARY) == 0

        /** Keys of all live WhatsApp and Proton Mail notifications; null when not connected. */
        fun liveKeys(): List<String>? =
            instance?.let { l -> runCatching { l.activeNotifications.orEmpty().filter { keep(it) }.map { it.key } }.getOrNull() }
    }
}
