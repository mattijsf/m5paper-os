package nl.paperos.companion.proto

import nl.paperos.companion.Hkdf
import org.json.JSONObject
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test
import java.io.File
import java.security.KeyFactory
import java.security.spec.PKCS8EncodedKeySpec
import java.security.spec.X509EncodedKeySpec
import javax.crypto.KeyAgreement

/** docs/COMPANION.md section 9: the same bytes as tests/fixtures/companion/vectors.json. */
class ProtoVectorsTest {
    private val v = JSONObject(File(System.getProperty("vectors") ?: "../../tests/fixtures/companion/vectors.json").readText())

    private fun hex(s: String) = ByteArray(s.length / 2) { s.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
    private fun ByteArray.hex() = joinToString("") { "%02x".format(it) }
    private fun h(name: String) = hex(v.getString(name))

    /** X25519 with a raw scalar, through the JDK's XDH (the same JCA API as on Android). */
    private fun x25519(sk: ByteArray, pk: ByteArray): ByteArray {
        val priv = KeyFactory.getInstance("XDH").generatePrivate(PKCS8EncodedKeySpec(hex("302e020100300506032b656e04220420") + sk))
        val pub = KeyFactory.getInstance("XDH").generatePublic(X509EncodedKeySpec(hex("302a300506032b656e032100") + pk))
        val ka = KeyAgreement.getInstance("XDH")
        ka.init(priv)
        ka.doPhase(pub, true)
        return ka.generateSecret()
    }

    private val basePoint = ByteArray(32).also { it[0] = 9 }
    private val pid get() = h("pid")
    private val keys get() = KeySchedule.derive(h("shared_secret"), pid, h("paper_pk"), h("phone_pk"))

    @Test fun publicKeysAndSharedSecret() {
        assertEquals(v.getString("paper_pk"), x25519(h("paper_sk"), basePoint).hex())
        assertEquals(v.getString("phone_pk"), x25519(h("phone_sk"), basePoint).hex())
        assertEquals(v.getString("shared_secret"), x25519(h("phone_sk"), h("paper_pk")).hex())
        assertEquals(v.getString("shared_secret"), x25519(h("paper_sk"), h("phone_pk")).hex())
        assertEquals(v.getString("paper_pk_b64"), B64.enc(h("paper_pk")))
        assertEquals(v.getString("pid_b64"), B64.enc(pid))
    }

    @Test fun keySchedule() {
        assertEquals(v.getString("prk"), Hkdf.extract("PaperOS companion v1".toByteArray(), h("shared_secret")).hex())
        val k = keys
        assertEquals(v.getString("k_p2d"), k.p2d.hex())
        assertEquals(v.getString("k_d2p"), k.d2p.hex())
        assertEquals(v.getString("code"), k.code)
        assertEquals(v.getString("code").let { it.substring(0, 3) + " " + it.substring(3) }, k.codeShown)
    }

    @Test(expected = IllegalArgumentException::class) fun zeroSharedSecretRejected() {
        KeySchedule.derive(ByteArray(32), pid, h("paper_pk"), h("phone_pk"))
    }

    @Test fun qrText() {
        val q = QrPayload.parse(v.getString("qr_text"))!!
        assertArrayEquals(pid, q.pid)
        assertArrayEquals(h("paper_pk"), q.paperPk)
        assertEquals("https://ha.example.test", q.baseUrl)
        val withPort = QrPayload.parse("paperos1:${v.getString("pid_b64")}:${v.getString("paper_pk_b64")}:https://ha.example.test:8443/")!!
        assertEquals("https://ha.example.test:8443", withPort.baseUrl)
        assertNull(QrPayload.parse("paperos2:" + v.getString("qr_text").substringAfter(":")))
        assertNull(QrPayload.parse("paperos1:AAAA:${v.getString("paper_pk_b64")}:https://x.test"))
        assertNull(QrPayload.parse("paperos1:${v.getString("pid_b64")}:${v.getString("paper_pk_b64")}:ftp://x.test"))
        assertNull(QrPayload.parse("hello"))
    }

    @Test fun helloEnvelope() {
        val o = v.getJSONObject("hello")
        val env = Envelope.sealHello(keys.p2d, pid, h("phone_pk"), o.getString("plaintext").toByteArray(), hex(o.getString("nonce")))
        assertEquals(o.getString("envelope"), env)
        assertArrayEquals(h("phone_pk"), Envelope.helloPhonePk(env))
        assertEquals(o.getString("plaintext"), String(Envelope.openHello(keys.p2d, pid, env)!!))
    }

    @Test fun postEnvelope() {
        val o = v.getJSONObject("post")
        val pt = o.getString("plaintext").toByteArray(Charsets.UTF_8)
        val env = Envelope.seal(keys.p2d, Envelope.DIR_P, pid, o.getLong("counter"), pt, hex(o.getString("nonce")))
        assertEquals(o.getString("envelope"), env)
        val opened = Envelope.open(keys.p2d, Envelope.DIR_P, pid, env)!!
        assertEquals(1L, opened.counter)
        assertArrayEquals(pt, opened.plaintext)
    }

    @Test fun replyEnvelope() {
        val o = v.getJSONObject("reply")
        val pt = o.getString("plaintext").toByteArray()
        assertEquals(o.getString("envelope"), Envelope.seal(keys.d2p, Envelope.DIR_D, pid, 1, pt, hex(o.getString("nonce"))))
        val opened = Envelope.open(keys.d2p, Envelope.DIR_D, pid, o.getString("envelope"))!!
        val msg = Messages.parse(opened.plaintext) as Messages.Incoming.Reply
        assertEquals("0|com.whatsapp|1|test|10001", msg.key)
        assertEquals("See you at six", msg.text)
        assertEquals(1768480100L, msg.ts)
    }

    @Test fun rejected() {
        val r = v.getJSONObject("reject_to_paper")
        for (name in listOf("flipped_tag", "flipped_counter", "wrong_pid", "wrong_dir_reply_as_post", "oversize")) {
            assertNull(name, Envelope.open(keys.p2d, Envelope.DIR_P, pid, r.getString(name)))
        }
        // The reply is fine as D but not with the other key or another pid.
        val reply = v.getJSONObject("reply").getString("envelope")
        assertNotNull(Envelope.open(keys.d2p, Envelope.DIR_D, pid, reply))
        assertNull(Envelope.open(keys.p2d, Envelope.DIR_D, pid, reply))
        assertNull(Envelope.open(keys.d2p, Envelope.DIR_D, ByteArray(8), reply))
        assertNull(Envelope.open(keys.d2p, Envelope.DIR_D, pid, reply + "A"))
        assertNull(Envelope.open(keys.d2p, Envelope.DIR_D, pid, "not base64!"))
    }

    @Test fun receivePolicy() {
        val reply = v.getJSONObject("reply").getString("envelope")
        val k = keys
        val ok = ReceivePolicy.accept(listOf(QueueEntry(1, 0, reply)), k.d2p, Envelope.DIR_D, pid, 0)
        assertEquals(1, ok.first.size)
        assertEquals(1L, ok.first[0].counter)
        // Not above rx, or the entry id does not match the counter: dropped.
        assertEquals(0 to 1, ReceivePolicy.accept(listOf(QueueEntry(1, 0, reply)), k.d2p, Envelope.DIR_D, pid, 1).let { it.first.size to it.second })
        assertEquals(0 to 1, ReceivePolicy.accept(listOf(QueueEntry(2, 0, reply)), k.d2p, Envelope.DIR_D, pid, 0).let { it.first.size to it.second })
        // Order by counter, whatever order the queue lists them in.
        val e2 = Envelope.seal(k.d2p, Envelope.DIR_D, pid, 2, """{"t":"dismiss","keys":[]}""".toByteArray())
        val e3 = Envelope.seal(k.d2p, Envelope.DIR_D, pid, 3, """{"t":"unpair"}""".toByteArray())
        val got = ReceivePolicy.accept(listOf(QueueEntry(3, 0, e3), QueueEntry(1, 0, reply), QueueEntry(2, 0, e2)), k.d2p, Envelope.DIR_D, pid, 0)
        assertEquals(listOf(1L, 2L, 3L), got.first.map { it.counter })
    }

    @Test fun ownEnvelopesRoundTripWithRandomNonce() {
        val k = keys
        val a = Envelope.seal(k.p2d, Envelope.DIR_P, pid, 7, "{}".toByteArray())
        val b = Envelope.seal(k.p2d, Envelope.DIR_P, pid, 7, "{}".toByteArray())
        assert(a != b) { "nonce must be random" }
        assertEquals(7L, Envelope.open(k.p2d, Envelope.DIR_P, pid, a)!!.counter)
        val max = Envelope.seal(k.p2d, Envelope.DIR_P, pid, 1, ByteArray(1000) { 'x'.code.toByte() })
        assertEquals(Envelope.MAX_CHARS, max.length)
    }
}
