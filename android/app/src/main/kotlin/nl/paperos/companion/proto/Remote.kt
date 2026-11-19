package nl.paperos.companion.proto

import nl.paperos.companion.Hkdf
import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.io.Closeable
import java.io.IOException
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

/**
 * Remote control of the Paper from this phone (docs/COMPANION.md section 10, the Paper's src/core/remote_phone.h): the Paper's
 * wireless development port (Settings > Developer > Wireless development), signed in with a key from the pairing. The phone may
 * ask for the screen and send touches; the Paper refuses everything else. Plain JVM: unit-tested against a fake Paper.
 */
object RemoteAuth {
    const val PORT = 8266

    /** HKDF-Expand(prk = k_p2d, info = "PaperOS remote", 32). */
    fun key(p2d: ByteArray): ByteArray = Hkdf.expand(p2d, "PaperOS remote".toByteArray(Charsets.US_ASCII), 32)

    /** Hex of HMAC-SHA256(key, "paperos-remote:" + nonce hex). */
    fun proof(key: ByteArray, nonceHex: String): String {
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(key, "HmacSHA256"))
        return mac.doFinal("paperos-remote:$nonceHex".toByteArray(Charsets.US_ASCII)).joinToString("") { "%02x".format(it) }
    }

    /** An address the Paper may give (`remote` message) or the user may type: IPv4 or a host name, nothing else. */
    fun validHost(h: String): Boolean = h.length in 1..253 && h.all { it.isLetterOrDigit() && it.code < 128 || it == '.' || it == '-' }
}

/** The Paper's frame: 4-bit grays, two pixels per byte, the left one in the high nibble (gray 0 = ink .. 15 = paper). */
class Frame(val width: Int, val height: Int, val packed: ByteArray, val crc: String) {
    /** One colour per pixel from a 16-entry palette. */
    fun pixels(palette: IntArray): IntArray {
        val out = IntArray(width * height)
        val n = minOf(packed.size, out.size / 2)
        for (i in 0 until n) {
            val b = packed[i].toInt()
            out[2 * i] = palette[(b shr 4) and 15]
            out[2 * i + 1] = palette[b and 15]
        }
        return out
    }
}

class RemoteException(message: String) : IOException(message)

/**
 * A firmware package as tools/ota.py packs it (the Paper's src/core/update.h): a 148-byte header, then the image. The phone
 * reads the header to show what it is; it cannot check the signature (it has no key): the Paper does, before it writes a byte.
 */
class FirmwarePackage(val bytes: ByteArray, val build: String, val imageSize: Long, val madeUnix: Long) {
    companion object {
        const val HEADER = 148
        private val MAGIC = "POSOTA1\n".toByteArray(Charsets.US_ASCII)

        /** Null when it is not a package (wrong magic, a size that does not match the file). */
        fun parse(b: ByteArray): FirmwarePackage? {
            if (b.size < HEADER || !b.copyOfRange(0, 8).contentEquals(MAGIC)) return null
            val build = String(b, 8, 32, Charsets.US_ASCII).trimEnd('\u0000')
            if (build.isEmpty() || !build.all { it.isLetterOrDigit() || it == '-' || it == '.' || it == ' ' }) return null
            fun le(off: Int, n: Int): Long = (0 until n).fold(0L) { acc, i -> acc or ((b[off + i].toLong() and 0xFF) shl (8 * i)) }
            val size = le(40, 4)
            if (size != (b.size - HEADER).toLong()) return null
            return FirmwarePackage(b, build, size, le(76, 8))
        }
    }
}

/** One signed-in connection. Not thread-safe: one thread owns it. */
class RemoteLink private constructor(private val socket: Socket) : Closeable {
    private val input = BufferedInputStream(socket.getInputStream(), 64 * 1024)
    private val output: OutputStream = socket.getOutputStream()
    var build: String = ""
        private set

    companion object {
        const val WIDTH = 540
        const val HEIGHT = 960
        private const val CHUNK = 4096

        /** Connects and signs in; throws [RemoteException] with a reason the user can act on, or an [IOException]. */
        fun open(host: String, port: Int, key: ByteArray, connectTimeoutMs: Int = 3000, readTimeoutMs: Int = 10000): RemoteLink {
            val s = Socket()
            try {
                s.tcpNoDelay = true
                s.connect(InetSocketAddress(host, port), connectTimeoutMs)
                s.soTimeout = readTimeoutMs
                val link = RemoteLink(s)
                link.signIn(key)
                return link
            } catch (e: IOException) {
                runCatching { s.close() }
                throw e
            }
        }
    }

    private fun signIn(key: ByteArray) {
        val hello = readLine()
        val parts = hello.split(' ')
        if (parts.size != 5 || parts[0] != "#hello" || parts[1] != "paperos" || parts[2] != "1") throw RemoteException("Not a Paper")
        build = parts[3]
        send(">auth-phone ${RemoteAuth.proof(key, parts[4])}")
        val answer = readLine()
        if (answer != "#ok") throw RemoteException(if (answer.startsWith("#err")) "The Paper did not accept this phone" else "Unexpected answer")
    }

    /** The current frame. */
    fun shot(): Frame {
        send(">shot")
        val head = readLine().split(' ')
        if (head.size != 5 || head[0] != "#shot") throw RemoteException(head.joinToString(" ").ifEmpty { "No frame" })
        val w = head[1].toInt()
        val h = head[2].toInt()
        val n = head[3].toInt()
        if (w != WIDTH || h != HEIGHT || n != w * h / 2) throw RemoteException("Unexpected frame size")
        val px = readBytes(n)
        expectOk()
        return Frame(w, h, px, head[4])
    }

    /** The Paper's refresh log: it changes whenever the panel was redrawn (cheap to ask often). */
    fun refreshMark(): Int {
        send(">refresh")
        val head = readLine().split(' ')
        if (head.size != 2 || head[0] != "#refresh") throw RemoteException("No refresh log")
        val body = readBytes(head[1].toInt())
        expectOk()
        return body.contentHashCode()
    }

    /**
     * Sends a firmware package (`>ota <size>`, the Paper's src/core/remote.h): 4 KB pieces, each full one acknowledged.
     * Returns when the Paper says the image is in place; it then restarts into it (and closes this connection).
     * Throws [RemoteException] with the Paper's reason when it refuses (not signed with its key, ...).
     */
    fun ota(pkg: FirmwarePackage, progress: (sent: Int, total: Int) -> Unit) {
        val data = pkg.bytes
        send(">ota ${data.size}")
        val go = readLine()
        if (!go.startsWith("#ota go")) throw RemoteException(refusal(go))
        var sent = 0
        while (sent < data.size) {
            val n = minOf(CHUNK, data.size - sent)
            output.write(data, sent, n)
            output.flush()
            sent += n
            if (sent < data.size && sent % CHUNK == 0) {
                val ack = readUntil("#ack")
                if (!ack.startsWith("#ack")) throw RemoteException(refusal(ack))
            }
            progress(sent, data.size)
        }
        val end = readUntil("#ok") // after the last byte the Paper acks the total once more, then says #ok (as tools/ota.py reads it)
        if (end != "#ok") throw RemoteException(refusal(end))
    }

    /** The next line that starts with [want] or "#err" (other lines, such as a late "#ack", are skipped). */
    private fun readUntil(want: String): String {
        while (true) {
            val l = readLine()
            if (l.startsWith(want) || l.startsWith("#err")) return l
        }
    }

    private fun refusal(line: String) = line.removePrefix("#err ").ifEmpty { "No answer" }

    fun tap(x: Int, y: Int) = command(">tap ${cx(x)} ${cy(y)}")
    fun longPress(x: Int, y: Int) = command(">long ${cx(x)} ${cy(y)}")
    fun swipe(x1: Int, y1: Int, x2: Int, y2: Int) = command(">swipe ${cx(x1)} ${cy(y1)} ${cx(x2)} ${cy(y2)}")

    private fun cx(x: Int) = x.coerceIn(0, WIDTH - 1)
    private fun cy(y: Int) = y.coerceIn(0, HEIGHT - 1)

    /** One command line, its first answer line (device tests: what the Paper refuses a phone). */
    internal fun firstAnswer(line: String): String {
        send(line)
        return readLine()
    }

    private fun command(line: String) {
        send(line)
        expectOk()
    }

    private fun expectOk() {
        val l = readLine()
        if (l != "#ok") throw RemoteException(l.ifEmpty { "No answer" })
    }

    private fun send(line: String) {
        output.write("$line\n".toByteArray(Charsets.US_ASCII))
        output.flush()
    }

    private fun readLine(): String {
        val b = ByteArrayOutputStream()
        while (true) {
            val c = input.read()
            if (c < 0) throw IOException("The Paper closed the connection")
            if (c == '\n'.code) break
            if (b.size() < 512) b.write(c)
        }
        return b.toString(Charsets.US_ASCII.name()).trimEnd('\r')
    }

    private fun readBytes(n: Int): ByteArray {
        val out = ByteArray(n)
        var got = 0
        while (got < n) {
            val r = input.read(out, got, n - got)
            if (r < 0) throw IOException("The Paper closed the connection")
            got += r
        }
        return out
    }

    override fun close() {
        runCatching { socket.close() }
    }
}
