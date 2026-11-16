package nl.paperos.companion

import android.app.Activity
import android.content.ClipData
import android.content.ClipDescription
import android.content.ClipboardManager
import android.content.Context
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Toast
import nl.paperos.companion.proto.Messages

/**
 * The clipboard shared with the Paper (DECISIONS.md D23, docs/COMPANION.md 7 `clip`).
 *
 * Paper to phone: a `clip` envelope replaces the phone's clipboard (when "Share the clipboard" is on and the envelope is at
 * most 24 hours old). Phone to Paper: only on the user's action, because Android (10+) lets an app read the clipboard only
 * while it has focus: the share sheet ([ShareToPaperActivity]), the Quick Settings tile ([ClipTileService] opens
 * [ClipToPaperActivity]) and the button in the app. A clipboard marked sensitive (a password manager's copy) is never sent.
 * The text is never logged or stored by this app.
 */
object ClipSync {
    const val LABEL = "PaperOS"

    fun fromPaper(ctx: Context, clip: Messages.Incoming.Clip, entryTs: Long) {
        if (!Link.clipboardShared) return
        val now = System.currentTimeMillis() / 1000
        val newest = maxOf(clip.ts, entryTs)
        if (clip.text.isBlank() || now - newest > Messages.CLIP_MAX_AGE_S) return
        val app = ctx.applicationContext
        Handler(Looper.getMainLooper()).post {
            runCatching {
                app.getSystemService(ClipboardManager::class.java).setPrimaryClip(ClipData.newPlainText(LABEL, clip.text))
            }
        }
    }

    /** Reads the clipboard (call only while an activity of this app has focus) and sends it. A toast says what happened. */
    fun sendCurrent(activity: Activity) {
        val cm = activity.getSystemService(ClipboardManager::class.java)
        val clip = cm.primaryClip
        val sensitive = clip?.description?.extras?.getBoolean(ClipDescription.EXTRA_IS_SENSITIVE) == true
        val text = if (sensitive || clip == null || clip.itemCount == 0) null else clip.getItemAt(0).coerceToText(activity)?.toString()
        when {
            sensitive -> toast(activity, "Not sent: the clipboard is marked sensitive")
            else -> report(activity, Link.sendClip(activity, text))
        }
    }

    fun report(ctx: Context, r: Link.ClipSent) = toast(
        ctx,
        when (r) {
            Link.ClipSent.SENT -> "Sent to the Paper"
            Link.ClipSent.OFF -> "Clipboard sharing is off in PaperOS Companion"
            Link.ClipSent.NOT_PAIRED -> "Not paired with a Paper"
            Link.ClipSent.EMPTY -> "Nothing to send: the clipboard is empty"
        },
    )

    private fun toast(ctx: Context, text: String) = Toast.makeText(ctx.applicationContext, text, Toast.LENGTH_SHORT).show()
}

/** Share sheet target "Send to Paper": plain text from any app. No UI of its own. */
class ShareToPaperActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val text = intent?.takeIf { it.action == android.content.Intent.ACTION_SEND }?.getStringExtra(android.content.Intent.EXTRA_TEXT)
        ClipSync.report(this, Link.sendClip(this, text))
        finish()
    }
}

/** Opened by the tile: waits for window focus (Android lets only the focused app read the clipboard), sends, closes. */
class ClipToPaperActivity : Activity() {
    private var done = false

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (!hasFocus || done) return
        done = true
        ClipSync.sendCurrent(this)
        finish()
    }
}
