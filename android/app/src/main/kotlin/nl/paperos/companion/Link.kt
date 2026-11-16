package nl.paperos.companion

import android.content.Context
import android.os.Build
import nl.paperos.companion.proto.Envelope
import nl.paperos.companion.proto.KeySchedule
import nl.paperos.companion.proto.Messages
import nl.paperos.companion.proto.QrPayload
import nl.paperos.companion.proto.QueueEntry
import nl.paperos.companion.proto.ReceivePolicy
import nl.paperos.companion.proto.ReplyGuard
import nl.paperos.companion.proto.SessionKeys
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * Pairing state and the phone side of docs/COMPANION.md sections 2, 5 and 7.
 *
 * Stored in noBackupFilesDir/pairing.json: pid, paper_pk, phone_pk, stage, counters. No secrets:
 * the phone's private key stays in the Android Keystore ([KeyVault]) and the session keys are
 * derived again in memory after a restart. Nothing here logs plaintext, keys or the token.
 */
object Link {
    enum class Stage { NONE, PENDING, ACTIVE }

    private const val PAIRING_TTL_MS = 15 * 60 * 1000L
    private const val SYNC_MIN_GAP_MS = 5 * 60 * 1000L

    private var loaded = false
    var stage = Stage.NONE; private set
    private var pid = ByteArray(0)
    private var paperPk = ByteArray(0)
    private var phonePk = ByteArray(0)
    var userConfirmed = false; private set
    var pairedSeen = false; private set
    private var pendingSince = 0L
    private var tx = 0L
    private var rx = 0L
    private var lastSyncMs = 0L
    @Volatile private var keys: SessionKeys? = null

    // Settings that survive re-pairing.
    var baseUrl: String? = null; private set
    var repliesAllowed = false; private set
    private val sends = mutableListOf<Long>()

    private fun pairingFile(ctx: Context) = File(ctx.noBackupFilesDir, "pairing.json")
    private fun settingsFile(ctx: Context) = File(ctx.noBackupFilesDir, "settings.json")

    @Synchronized
    fun load(ctx: Context) {
        if (loaded) return
        loaded = true
        runCatching {
            val o = JSONObject(settingsFile(ctx).readText())
            baseUrl = o.optString("url").ifEmpty { null }
            repliesAllowed = o.optBoolean("replies", false)
            o.optJSONArray("sends")?.let { a -> for (i in 0 until a.length()) sends += a.getLong(i) }
        }
        runCatching {
            val o = JSONObject(pairingFile(ctx).readText())
            stage = Stage.valueOf(o.getString("stage"))
            pid = hex(o.getString("pid"))
            paperPk = hex(o.getString("paper_pk"))
            phonePk = hex(o.getString("phone_pk"))
            userConfirmed = o.optBoolean("confirmed")
            pairedSeen = o.optBoolean("paired_seen")
            pendingSince = o.optLong("pending_since")
            tx = o.optLong("tx")
            rx = o.optLong("rx")
            lastSyncMs = o.optLong("last_sync")
        }.onFailure { stage = Stage.NONE }
        if (stage != Stage.NONE) {
            keys = runCatching { derive(ctx) }.getOrNull()
            if (keys == null) forget(ctx, notifyPaper = false)
        }
    }

    private fun derive(ctx: Context): SessionKeys {
        val vault = KeyVault(ctx)
        check(vault.publicKey().contentEquals(phonePk)) { "phone key changed" }
        val ss = vault.agree(paperPk)
        try {
            return KeySchedule.derive(ss, pid, paperPk, phonePk)
        } finally {
            ss.fill(0)
        }
    }

    // ---- pairing

    /** Starts pairing from a scanned QR text. Returns the comparison code ("123 456"). */
    @Synchronized
    fun startPairing(ctx: Context, qrText: String): String {
        load(ctx)
        val qr = QrPayload.parse(qrText) ?: throw IllegalArgumentException("Not a PaperOS pairing code")
        require(qr.baseUrl.startsWith("https://")) { "The Home Assistant address must use https" }
        val vault = KeyVault(ctx)
        vault.delete()
        vault.ensure()
        pid = qr.pid
        paperPk = qr.paperPk
        phonePk = vault.publicKey()
        val k = derive(ctx)
        keys = k
        stage = Stage.PENDING
        userConfirmed = false
        pairedSeen = false
        pendingSince = System.currentTimeMillis()
        tx = 0
        rx = 0
        lastSyncMs = 0
        baseUrl = qr.baseUrl
        saveSettings(ctx)
        savePairing(ctx)
        val box = Outbox.of(ctx)
        box.clear()
        val hello = Envelope.sealHello(k.p2d, pid, phonePk, Messages.hello(Build.MODEL, BuildInfo.version(ctx)))
        box.add(item("pairing", 0, hello))
        RelayService.kick(ctx)
        return k.codeShown
    }

    fun code(): String? = keys?.codeShown
    fun pidText(): String? = if (stage == Stage.NONE) null else nl.paperos.companion.proto.B64.enc(pid)

    fun pairingExpired(): Boolean =
        stage == Stage.PENDING && !pairedSeen && System.currentTimeMillis() - pendingSince > PAIRING_TTL_MS

    @Synchronized
    fun confirmCode(ctx: Context) {
        load(ctx)
        if (stage != Stage.PENDING) return
        userConfirmed = true
        savePairing(ctx)
        maybeActivate(ctx)
    }

    private fun maybeActivate(ctx: Context) {
        if (stage == Stage.PENDING && userConfirmed && pairedSeen) {
            stage = Stage.ACTIVE
            savePairing(ctx)
            sendSync(ctx, force = true)
        }
    }

    /** Forgets the pairing. With [notifyPaper], first queues a best-effort `unpair`. */
    @Synchronized
    fun forget(ctx: Context, notifyPaper: Boolean) {
        val notify = notifyPaper && stage == Stage.ACTIVE
        Outbox.of(ctx).clear() // nothing of this pairing is useful to the Paper any more
        if (notify) runCatching { sendLocked(ctx, Messages.unpair()) }
        KeyVault(ctx).delete()
        keys = null
        stage = Stage.NONE
        pid = ByteArray(0); paperPk = ByteArray(0); phonePk = ByteArray(0)
        userConfirmed = false; pairedSeen = false; tx = 0; rx = 0
        pairingFile(ctx).delete()
    }

    @Synchronized
    fun setRepliesAllowed(ctx: Context, allowed: Boolean) {
        load(ctx)
        repliesAllowed = allowed
        saveSettings(ctx)
    }

    // ---- phone to Paper

    /** Seals and queues one message. Only while ACTIVE (section 2: before that, only the hello). */
    @Synchronized
    fun send(ctx: Context, plaintext: ByteArray?): Boolean {
        load(ctx)
        if (stage != Stage.ACTIVE || plaintext == null) return false
        sendLocked(ctx, plaintext)
        return true
    }

    private fun sendLocked(ctx: Context, plaintext: ByteArray) {
        val k = keys ?: return
        tx += 1
        savePairing(ctx) // tx is stored before the envelope can be pushed (section 5)
        Outbox.of(ctx).add(item("to_paper", tx, Envelope.seal(k.p2d, Envelope.DIR_P, pid, tx, plaintext)))
        RelayService.kick(ctx)
    }

    /** `sync` with all live keys. Without [force], at most every five minutes. */
    @Synchronized
    fun sendSync(ctx: Context, force: Boolean = false) {
        load(ctx)
        if (stage != Stage.ACTIVE) return
        val now = System.currentTimeMillis()
        if (!force && now - lastSyncMs < SYNC_MIN_GAP_MS) return
        val live = CaptureListener.liveKeys() ?: return
        lastSyncMs = now
        savePairing(ctx)
        send(ctx, Messages.sync(live))
    }

    fun hoursSinceSync(): Double = (System.currentTimeMillis() - lastSyncMs) / 3_600_000.0

    // ---- Paper to phone

    class ReceiveReport(val accepted: Int, val dropped: Int, val ackUpto: Long?)

    /** Handles the to_phone queue in counter order. Returns what to ack. */
    fun receive(ctx: Context, entries: List<QueueEntry>): ReceiveReport {
        val (accepted, dropped, startRx) = synchronized(this) {
            load(ctx)
            val k = keys ?: return ReceiveReport(0, entries.size, null)
            val r = ReceivePolicy.accept(entries, k.d2p, Envelope.DIR_D, pid, rx)
            Triple(r.first, r.second, rx)
        }
        var n = 0
        for (a in accepted) {
            val msg = Messages.parse(a.plaintext)
            val fresh = synchronized(this) {
                if (stage == Stage.NONE || a.counter <= rx) {
                    false
                } else {
                    rx = a.counter
                    savePairing(ctx) // before acting: never more than one send per counter, also after a restart
                    true
                }
            }
            if (!fresh) continue
            n++
            when (msg) {
                is Messages.Incoming.Paired -> synchronized(this) {
                    if (a.counter == 1L && stage == Stage.PENDING) {
                        pairedSeen = true
                        savePairing(ctx)
                        maybeActivate(ctx)
                    }
                }
                is Messages.Incoming.Reply -> if (stage == Stage.ACTIVE) handleReply(ctx, a.counter, a.entry.ts, msg)
                is Messages.Incoming.Unpair -> {
                    forget(ctx, notifyPaper = false)
                    return ReceiveReport(n, dropped, null)
                }
                is Messages.Incoming.Dismiss, Messages.Incoming.Other -> Unit // v1: nothing on the phone
            }
        }
        val upto = synchronized(this) { if (rx > startRx) rx else null }
        return ReceiveReport(n, dropped, upto)
    }

    private fun handleReply(ctx: Context, counter: Long, entryTs: Long, r: Messages.Incoming.Reply) {
        val now = System.currentTimeMillis() / 1000
        val listener = CaptureListener.instance
        val live = listener?.hasReplyAction(r.key) == true
        val history = synchronized(this) { sends.toList() }
        var why = ReplyGuard.check(live, r.text, r.ts, entryTs, now, history)
        if (why.isEmpty() && !repliesAllowed) why = "failed" // switched off in the app (default)
        if (why.isEmpty()) {
            synchronized(this) {
                sends += now
                sends.removeAll { now - it > 3600 }
                saveSettings(ctx)
            }
            why = if (listener?.sendReply(r.key, r.text.trim()) == true) "" else "failed"
        }
        send(ctx, Messages.result(counter, why))
    }

    // ---- storage

    private fun item(queue: String, id: Long, env: String): ByteArray =
        JSONObject().put("q", queue).put("id", id).put("e", env).toString().toByteArray()

    private fun savePairing(ctx: Context) {
        if (stage == Stage.NONE) { pairingFile(ctx).delete(); return }
        val o = JSONObject().put("stage", stage.name).put("pid", hex(pid)).put("paper_pk", hex(paperPk))
            .put("phone_pk", hex(phonePk)).put("confirmed", userConfirmed).put("paired_seen", pairedSeen)
            .put("pending_since", pendingSince).put("tx", tx).put("rx", rx).put("last_sync", lastSyncMs)
        writeAtomic(pairingFile(ctx), o.toString())
    }

    private fun saveSettings(ctx: Context) {
        val o = JSONObject().put("url", baseUrl ?: "").put("replies", repliesAllowed).put("sends", JSONArray(sends))
        writeAtomic(settingsFile(ctx), o.toString())
    }

    private fun writeAtomic(f: File, s: String) {
        val tmp = File(f.parentFile, f.name + ".tmp")
        tmp.writeText(s)
        check(tmp.renameTo(f)) { "could not store ${f.name}" }
    }

    private fun hex(b: ByteArray) = b.joinToString("") { "%02x".format(it) }
    private fun hex(s: String) = ByteArray(s.length / 2) { s.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
}

object BuildInfo {
    fun version(ctx: Context): String =
        runCatching { ctx.packageManager.getPackageInfo(ctx.packageName, 0).versionName }.getOrNull() ?: "?"
}
