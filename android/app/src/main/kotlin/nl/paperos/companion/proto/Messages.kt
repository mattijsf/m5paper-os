package nl.paperos.companion.proto

import org.json.JSONArray
import org.json.JSONObject
import java.security.MessageDigest

/** docs/COMPANION.md section 7 messages. Builders cut fields so the plaintext stays <= 1000 bytes. */
object Messages {
    const val MAX_KEY = 120
    const val MAX_MSGS = 6
    const val MAX_CHAT = 100

    /** One message of a `post`. Mail uses [subj]; WhatsApp leaves it null. */
    data class Msg(val from: String, val text: String, val ts: Long, val subj: String? = null)

    fun hello(name: String, app: String): ByteArray =
        JSONObject().put("t", "hello").put("name", cut(name, 60)).put("app", cut(app, 20)).bytes()

    /** Keys longer than 120 characters go as "h:" + base64url(SHA-256(key)). */
    fun wireKey(key: String): String =
        if (key.length <= MAX_KEY) key else "h:" + B64.enc(MessageDigest.getInstance("SHA-256").digest(key.toByteArray()))

    /** Returns null only when even a minimal post cannot fit (never for sane input). */
    fun post(key: String, app: String, chat: String, reply: Boolean, ts: Long, msgs: List<Msg>): ByteArray? {
        val mail = app == "mail"
        var list = msgs.takeLast(MAX_MSGS).map {
            Msg(
                cut(it.from, if (mail) 80 else 60),
                cut(it.text, 500),
                it.ts,
                it.subj?.let { s -> cut(s, 150) },
            )
        }.toMutableList()
        val c = cut(chat, MAX_CHAT)
        fun build(): ByteArray {
            val a = JSONArray()
            list.forEach { m ->
                val o = JSONObject().put("from", m.from)
                if (m.subj != null) o.put("subj", m.subj)
                o.put("text", m.text).put("ts", m.ts)
                a.put(o)
            }
            return JSONObject().put("t", "post").put("k", wireKey(key)).put("app", app).put("chat", c)
                .put("reply", reply).put("ts", ts).put("msgs", a).bytes()
        }
        var out = build()
        // Oldest messages go first, then the longest text is halved, until it fits.
        while (out.size > Envelope.MAX_PLAINTEXT && list.size > 1) { list.removeAt(0); out = build() }
        while (out.size > Envelope.MAX_PLAINTEXT) {
            val i = list.indices.maxByOrNull { list[it].text.length } ?: return null
            val m = list[i]
            val longest = maxOf(m.text.length, m.subj?.length ?: 0, m.from.length)
            if (longest == 0) return null
            list[i] = when (longest) {
                m.text.length -> m.copy(text = cut(m.text, m.text.length / 2))
                m.subj?.length -> m.copy(subj = cut(m.subj!!, m.subj.length / 2))
                else -> m.copy(from = cut(m.from, m.from.length / 2))
            }
            out = build()
        }
        return out
    }

    fun remove(key: String, why: String): ByteArray = JSONObject().put("t", "remove").put("k", wireKey(key)).put("why", why).bytes()

    /** Null when it would exceed 1000 bytes (spec: then skipped). */
    fun sync(keys: Collection<String>): ByteArray? =
        JSONObject().put("t", "sync").put("keys", JSONArray(keys.map(::wireKey))).bytes()
            .takeIf { it.size <= Envelope.MAX_PLAINTEXT }

    fun result(ref: Long, why: String): ByteArray =
        JSONObject().put("t", "result").put("ref", ref).put("ok", why.isEmpty()).put("why", why).bytes()

    fun unpair(): ByteArray = JSONObject().put("t", "unpair").bytes()

    /** Paper to phone. Unknown types parse as [Other]. */
    sealed class Incoming {
        object Paired : Incoming()
        data class Reply(val key: String, val text: String, val ts: Long) : Incoming()
        data class Dismiss(val keys: List<String>) : Incoming()
        object Unpair : Incoming()
        object Other : Incoming()
    }

    fun parse(plaintext: ByteArray): Incoming {
        val o = runCatching { JSONObject(String(plaintext, Charsets.UTF_8)) }.getOrNull() ?: return Incoming.Other
        return when (o.optString("t")) {
            "paired" -> Incoming.Paired
            "reply" -> Incoming.Reply(o.optString("k"), o.optString("text"), o.optLong("ts", 0))
            "dismiss" -> Incoming.Dismiss(
                (o.optJSONArray("keys") ?: JSONArray()).let { a -> (0 until a.length()).map { a.optString(it) } },
            )
            "unpair" -> Incoming.Unpair
            else -> Incoming.Other
        }
    }

    /** Cuts to at most [max] characters without splitting a surrogate pair. */
    fun cut(s: String, max: Int): String {
        if (s.length <= max) return s
        var end = max
        if (end > 0 && Character.isHighSurrogate(s[end - 1])) end--
        return s.substring(0, end)
    }

    private fun JSONObject.bytes() = toString().toByteArray(Charsets.UTF_8)
}

/** Section 7.2 send guards, checked in this order. Returns "" when the send may go ahead. */
object ReplyGuard {
    const val MAX_TEXT = 900
    const val MAX_AGE_S = 6 * 3600L
    const val MIN_GAP_S = 10L
    const val PER_HOUR = 20

    fun check(live: Boolean, text: String, envTs: Long, entryTs: Long, now: Long, sends: List<Long>): String {
        if (!live) return "gone"
        if (kotlin.math.abs(now - envTs) > MAX_AGE_S || kotlin.math.abs(now - entryTs) > MAX_AGE_S) return "stale"
        val t = text.trim()
        if (t.isEmpty() || t.length > MAX_TEXT) return "too_long"
        if (sends.any { now - it < MIN_GAP_S }) return "too_soon"
        if (sends.count { now - it < 3600 } >= PER_HOUR) return "hourly_limit"
        return ""
    }
}
