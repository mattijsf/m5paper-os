package nl.paperos.companion.proto

import nl.paperos.companion.Hkdf
import java.nio.ByteBuffer
import java.security.SecureRandom
import java.util.Base64
import javax.crypto.AEADBadTagException
import javax.crypto.Cipher
import javax.crypto.spec.IvParameterSpec
import javax.crypto.spec.SecretKeySpec

/**
 * docs/COMPANION.md v1: key schedule, envelopes, QR payload, receive policy. Pure JVM (no Android
 * classes), so it runs in host unit tests against tests/fixtures/companion/vectors.json.
 * Nothing here logs; keys and plaintexts stay in memory.
 */

object B64 {
    private val ALPHABET = Regex("^[A-Za-z0-9_-]*$")
    fun enc(b: ByteArray): String = Base64.getUrlEncoder().withoutPadding().encodeToString(b)
    fun dec(s: String): ByteArray? =
        if (!ALPHABET.matches(s) || s.length % 4 == 1) null else runCatching { Base64.getUrlDecoder().decode(s) }.getOrNull()
}

class SessionKeys(
    val pid: ByteArray,
    val paperPk: ByteArray,
    val phonePk: ByteArray,
    val p2d: ByteArray,
    val d2p: ByteArray,
    /** Six digits, leading zeros kept. */
    val code: String,
) {
    val codeShown: String get() = code.substring(0, 3) + " " + code.substring(3)
}

object KeySchedule {
    private val SALT = "PaperOS companion v1".toByteArray(Charsets.US_ASCII)

    fun derive(ss: ByteArray, pid: ByteArray, paperPk: ByteArray, phonePk: ByteArray): SessionKeys {
        require(ss.size == 32 && ss.any { it != 0.toByte() }) { "bad shared secret" }
        require(pid.size == 8 && paperPk.size == 32 && phonePk.size == 32) { "bad key sizes" }
        val transcript = pid + paperPk + phonePk
        val prk = Hkdf.extract(SALT, ss)
        fun expand(label: String, n: Int) = Hkdf.expand(prk, label.toByteArray(Charsets.US_ASCII) + transcript, n)
        val c = ByteBuffer.wrap(expand("PaperOS code", 4)).int.toLong() and 0xffffffffL
        val keys = SessionKeys(pid, paperPk, phonePk, expand("PaperOS p2d", 32), expand("PaperOS d2p", 32), "%06d".format(c % 1_000_000))
        prk.fill(0)
        return keys
    }
}

internal object Aead {
    /** Android (Conscrypt) and the JDK name the IETF cipher differently. */
    private fun cipher(): Cipher =
        runCatching { Cipher.getInstance("ChaCha20-Poly1305") }.getOrElse { Cipher.getInstance("ChaCha20/Poly1305/NoPadding") }

    fun seal(key: ByteArray, nonce: ByteArray, aad: ByteArray, pt: ByteArray): ByteArray {
        val c = cipher()
        c.init(Cipher.ENCRYPT_MODE, SecretKeySpec(key, "ChaCha20"), IvParameterSpec(nonce))
        c.updateAAD(aad)
        return c.doFinal(pt)
    }

    fun open(key: ByteArray, nonce: ByteArray, aad: ByteArray, ct: ByteArray): ByteArray? = try {
        val c = cipher()
        c.init(Cipher.DECRYPT_MODE, SecretKeySpec(key, "ChaCha20"), IvParameterSpec(nonce))
        c.updateAAD(aad)
        c.doFinal(ct)
    } catch (_: AEADBadTagException) {
        null
    } catch (_: java.security.GeneralSecurityException) {
        null
    }
}

object Envelope {
    const val VERSION: Byte = 0x01
    const val DIR_P: Byte = 0x50 // phone to Paper, k_p2d
    const val DIR_D: Byte = 0x44 // Paper to phone, k_d2p
    const val DIR_H: Byte = 0x48 // hello, k_p2d
    const val MAX_PLAINTEXT = 1000
    const val MAX_CHARS = 1395
    const val MAX_HELLO_CHARS = 1411
    private const val HEAD = 18
    private const val HELLO_HEAD = 42
    private const val NONCE = 12
    private const val TAG = 16

    private val rng = SecureRandom()
    fun nonce(): ByteArray = ByteArray(NONCE).also { rng.nextBytes(it) }

    fun seal(key: ByteArray, dir: Byte, pid: ByteArray, counter: Long, plaintext: ByteArray, nonce: ByteArray = nonce()): String {
        require(dir == DIR_P || dir == DIR_D) { "dir" }
        require(counter >= 1) { "counter must be >= 1" }
        require(plaintext.size <= MAX_PLAINTEXT) { "plaintext too long" }
        require(pid.size == 8 && nonce.size == NONCE)
        val head = ByteBuffer.allocate(HEAD).put(VERSION).put(dir).put(pid).putLong(counter).array()
        return B64.enc(head + nonce + Aead.seal(key, nonce, head, plaintext))
    }

    fun sealHello(kP2d: ByteArray, pid: ByteArray, phonePk: ByteArray, plaintext: ByteArray, nonce: ByteArray = nonce()): String {
        require(plaintext.size <= MAX_PLAINTEXT && pid.size == 8 && phonePk.size == 32 && nonce.size == NONCE)
        val head = byteArrayOf(VERSION, DIR_H) + pid + phonePk
        return B64.enc(head + nonce + Aead.seal(kP2d, nonce, head, plaintext))
    }

    class Opened(val counter: Long, val plaintext: ByteArray)

    /** Opens a normal envelope; null for anything not exactly right (size, version, dir, pid, counter, tag). */
    fun open(key: ByteArray, expectedDir: Byte, pid: ByteArray, env: String): Opened? {
        if (env.length > MAX_CHARS) return null
        val b = B64.dec(env) ?: return null
        if (b.size < HEAD + NONCE + TAG || b.size > HEAD + NONCE + MAX_PLAINTEXT + TAG) return null
        if (b[0] != VERSION || b[1] != expectedDir) return null
        if (!b.copyOfRange(2, 10).contentEquals(pid)) return null
        val counter = ByteBuffer.wrap(b, 10, 8).long
        if (counter < 1) return null
        val pt = Aead.open(key, b.copyOfRange(HEAD, HEAD + NONCE), b.copyOfRange(0, HEAD), b.copyOfRange(HEAD + NONCE, b.size))
            ?: return null
        if (pt.size > MAX_PLAINTEXT) return null
        return Opened(counter, pt)
    }

    /** For tests and diagnostics: the phone_pk inside a hello, without opening it. */
    fun helloPhonePk(env: String): ByteArray? {
        if (env.length > MAX_HELLO_CHARS) return null
        val b = B64.dec(env) ?: return null
        if (b.size < HELLO_HEAD + NONCE + TAG || b[0] != VERSION || b[1] != DIR_H) return null
        return b.copyOfRange(10, 42)
    }

    fun openHello(kP2d: ByteArray, pid: ByteArray, env: String): ByteArray? {
        if (env.length > MAX_HELLO_CHARS) return null
        val b = B64.dec(env) ?: return null
        if (b.size < HELLO_HEAD + NONCE + TAG || b.size > HELLO_HEAD + NONCE + MAX_PLAINTEXT + TAG) return null
        if (b[0] != VERSION || b[1] != DIR_H || !b.copyOfRange(2, 10).contentEquals(pid)) return null
        return Aead.open(kP2d, b.copyOfRange(HELLO_HEAD, HELLO_HEAD + NONCE), b.copyOfRange(0, HELLO_HEAD), b.copyOfRange(HELLO_HEAD + NONCE, b.size))
    }
}

/** `paperos1:<pid b64url>:<paper_pk b64url>:<HA base URL>`, split on the first three ':' only. */
class QrPayload(val pid: ByteArray, val paperPk: ByteArray, val baseUrl: String) {
    companion object {
        fun parse(text: String): QrPayload? {
            val parts = text.trim().split(":", limit = 4)
            if (parts.size != 4 || parts[0] != "paperos1") return null
            if (parts[1].length != 11 || parts[2].length != 43) return null
            val pid = B64.dec(parts[1])?.takeIf { it.size == 8 } ?: return null
            val pk = B64.dec(parts[2])?.takeIf { it.size == 32 } ?: return null
            val url = parts[3].trimEnd('/')
            if (!(url.startsWith("https://") || url.startsWith("http://")) || url.length < 10 || url.length > 300) return null
            if (url.any { it.isWhitespace() || it.code < 0x21 || it.code > 0x7e }) return null
            return QrPayload(pid, pk, url)
        }
    }
}

/** One entry of a relay queue sensor's `queue` attribute. */
class QueueEntry(val id: Long, val ts: Long, val e: String)

object ReceivePolicy {
    class Accepted(val entry: QueueEntry, val counter: Long, val plaintext: ByteArray)

    /** Section 5: sort by counter (= id), keep counter > rx, id == counter, AEAD opens. Returns them in order. */
    fun accept(entries: List<QueueEntry>, key: ByteArray, dir: Byte, pid: ByteArray, rx: Long): Pair<List<Accepted>, Int> {
        var last = rx
        var dropped = 0
        val out = mutableListOf<Accepted>()
        for (en in entries.sortedBy { it.id }) {
            val o = Envelope.open(key, dir, pid, en.e)
            if (o == null || o.counter <= last || o.counter != en.id) { dropped++; continue }
            out += Accepted(en, o.counter, o.plaintext)
            last = o.counter
        }
        return out to dropped
    }
}
