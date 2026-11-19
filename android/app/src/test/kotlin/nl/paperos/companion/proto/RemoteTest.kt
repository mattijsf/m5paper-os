package nl.paperos.companion.proto

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.ServerSocket
import kotlin.concurrent.thread

/**
 * Remote control (docs/COMPANION.md section 10). The values are the Paper's (tests/unit/remote_phone_test.cpp), from k_p2d of
 * tests/fixtures/companion/vectors.json. With -Dremote.sim=host:port it also runs against a simulator whose pairing is the
 * vectors' one (a UI test preset with Wireless development on).
 */
class RemoteTest {
    private fun hex(s: String) = ByteArray(s.length / 2) { s.substring(2 * it, 2 * it + 2).toInt(16).toByte() }

    private val p2d = hex("1efe7567b67386fc9d999a840aa50612210dc2e7c88a6349a5b15dcd6d0b20c1")
    private val remoteKey = hex("11d342f40c8190135181b05ae1957219d7456ca3343ee39672acb39f7f1e8175")
    private val nonce = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
    private val proof = "012199eacab31abc2c8e56351a8598c2d8f3393c532ad5558f9836bee7b1ce9e"

    @Test
    fun keyAndProofMatchThePaper() {
        assertArrayEquals(remoteKey, RemoteAuth.key(p2d))
        assertEquals(proof, RemoteAuth.proof(remoteKey, nonce))
    }

    @Test
    fun hostsAreAddressesOnly() {
        assertTrue(RemoteAuth.validHost("192.168.1.60"))
        assertTrue(RemoteAuth.validHost("paper.local"))
        for (bad in listOf("", "a b", "x/y", "1.2.3.4:80", "é.nl", "a".repeat(254))) assertFalse(bad, RemoteAuth.validHost(bad))
    }

    @Test
    fun remoteMessageGivesTheAddress() {
        assertEquals(
            Messages.Incoming.Remote("192.168.1.60", 8266),
            Messages.parse("""{"t":"remote","host":"192.168.1.60","port":8266}""".toByteArray()),
        )
        assertEquals(Messages.Incoming.Other, Messages.parse("""{"t":"remote","host":"http://x","port":8266}""".toByteArray()))
        assertEquals(Messages.Incoming.Other, Messages.parse("""{"t":"remote","host":"1.2.3.4","port":0}""".toByteArray()))
    }

    @Test
    fun framePixelsLeftNibbleFirst() {
        val f = Frame(4, 1, byteArrayOf(0x0F, 0x73), "x")
        assertArrayEquals(intArrayOf(0, 15, 7, 3), f.pixels(IntArray(16) { it }))
    }

    /** A fake Paper: the hello, the phone's sign-in, then a refresh log, a frame and a tap, as src/core/remote.cpp answers. */
    private fun fakePaper(acceptKey: ByteArray, seen: MutableList<String>): ServerSocket {
        val server = ServerSocket(0)
        thread(isDaemon = true) {
            server.accept().use { s ->
                val r = BufferedReader(InputStreamReader(s.getInputStream(), Charsets.US_ASCII))
                val out = s.getOutputStream()
                out.write("#hello paperos 1 abc123 $nonce\n".toByteArray())
                val auth = r.readLine()
                seen += auth
                if (auth != ">auth-phone ${RemoteAuth.proof(acceptKey, nonce)}") {
                    out.write("#err not the paired phone\n".toByteArray())
                    return@thread
                }
                out.write("#ok\n".toByteArray())
                while (true) {
                    val line = r.readLine() ?: break
                    seen += line
                    when {
                        line == ">refresh" -> out.write("#refresh 3\nabc#ok\n".toByteArray())
                        line == ">shot" -> {
                            out.write("#shot 540 960 259200 0badc0de\n".toByteArray())
                            out.write(ByteArray(259200) { 0x5A })
                            out.write("#ok\n".toByteArray())
                        }
                        line.startsWith(">tap ") -> out.write("#ok\n".toByteArray())
                        else -> out.write("#err not from the phone\n".toByteArray())
                    }
                }
            }
        }
        return server
    }

    @Test
    fun signsInAndTalksToTheFakePaper() {
        val seen = mutableListOf<String>()
        fakePaper(remoteKey, seen).use { server ->
            RemoteLink.open("127.0.0.1", server.localPort, RemoteAuth.key(p2d)).use { link ->
                assertEquals("abc123", link.build)
                link.refreshMark()
                val f = link.shot()
                assertEquals(540, f.width)
                assertEquals("0badc0de", f.crc)
                assertEquals(5, f.pixels(IntArray(16) { it })[0])
                link.tap(600, -3) // clamped to the panel
            }
        }
        Thread.sleep(100)
        assertEquals(listOf(">auth-phone $proof", ">refresh", ">shot", ">tap 539 0"), seen)
    }

    @Test
    fun aWrongKeyIsTurnedAway() {
        fakePaper(remoteKey, mutableListOf()).use { server ->
            try {
                RemoteLink.open("127.0.0.1", server.localPort, RemoteAuth.key(ByteArray(32))).close()
                fail("signed in with the wrong key")
            } catch (e: RemoteException) {
                assertEquals("The Paper did not accept this phone", e.message)
            }
        }
    }

    @Test
    fun againstTheSimulator() {
        val sim = System.getProperty("remote.sim")
        assumeTrue("set -Dremote.sim=localhost:8266 to run", !sim.isNullOrEmpty())
        val (host, port) = sim!!.split(":").let { it[0] to it[1].toInt() }
        RemoteLink.open(host, port, RemoteAuth.key(p2d)).use { link ->
            val before = link.shot()
            assertEquals(259200, before.packed.size)
            link.refreshMark()
            link.tap(270, 480)
            try {
                link.swipe(-5, 2000, 10, 10)
            } catch (e: RemoteException) {
                fail("swipe refused: ${e.message}")
            }
        }
    }
}
