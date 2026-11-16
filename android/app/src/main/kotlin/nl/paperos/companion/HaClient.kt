package nl.paperos.companion

import nl.paperos.companion.proto.QueueEntry
import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/**
 * Plain REST calls to the relay (docs/COMPANION.md section 6). The token goes only into the
 * Authorization header; nothing here logs. All Home Assistant data is parsed as untrusted.
 */
class HaClient(private val baseUrl: String, private val token: String) {
    class HttpError(val code: Int) : IOException("HTTP $code")

    /** Returns the HTTP status. */
    fun push(queue: String, id: Long, env: String): Int =
        post("/api/services/script/paperos_relay_push", JSONObject().put("queue", queue).put("id", id).put("env", env))

    fun ack(queue: String, upto: Long): Int =
        post("/api/services/script/paperos_relay_ack", JSONObject().put("queue", queue).put("upto", upto))

    /** Queue entries of a relay sensor; null when the sensor does not exist (404: relay not installed). */
    fun read(entity: String): List<QueueEntry>? {
        val c = open("/api/states/$entity", "GET")
        try {
            val code = c.responseCode
            if (code == 404) return null
            if (code != 200) throw HttpError(code)
            val body = c.inputStream.use { readCapped(it) }
            val q = JSONObject(body).optJSONObject("attributes")?.optJSONArray("queue") ?: return emptyList()
            val out = mutableListOf<QueueEntry>()
            for (i in 0 until minOf(q.length(), MAX_ENTRIES)) {
                val o = q.optJSONObject(i) ?: continue
                val e = o.optString("e")
                if (!ENV.matches(e)) continue
                val id = o.optLong("id", -1)
                if (id < 0) continue
                out += QueueEntry(id, o.optLong("ts", 0), e)
            }
            return out
        } finally {
            c.disconnect()
        }
    }

    private fun post(path: String, body: JSONObject): Int {
        val c = open(path, "POST")
        try {
            c.doOutput = true
            c.setRequestProperty("Content-Type", "application/json")
            c.outputStream.use { it.write(body.toString().toByteArray()) }
            val code = c.responseCode
            runCatching { (if (code < 400) c.inputStream else c.errorStream)?.use { readCapped(it) } }
            return code
        } finally {
            c.disconnect()
        }
    }

    private fun open(path: String, method: String): HttpURLConnection {
        val c = URL(baseUrl + path).openConnection() as HttpURLConnection
        c.requestMethod = method
        c.connectTimeout = 15_000
        c.readTimeout = 20_000
        c.instanceFollowRedirects = false
        c.setRequestProperty("Authorization", "Bearer $token")
        c.setRequestProperty("Accept", "application/json")
        return c
    }

    private fun readCapped(s: java.io.InputStream): String {
        val buf = java.io.ByteArrayOutputStream()
        val b = ByteArray(8192)
        while (true) {
            val n = s.read(b)
            if (n < 0) break
            buf.write(b, 0, n)
            if (buf.size() > MAX_BODY) throw IOException("response too large")
        }
        return buf.toString(Charsets.UTF_8.name())
    }

    companion object {
        private const val MAX_BODY = 512 * 1024
        private const val MAX_ENTRIES = 100
        val ENV = Regex("^[A-Za-z0-9_-]{40,1411}$")
    }
}
