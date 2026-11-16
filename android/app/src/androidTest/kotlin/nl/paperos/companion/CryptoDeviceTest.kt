package nl.paperos.companion

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith
import java.security.KeyFactory
import java.security.KeyPairGenerator
import java.security.SecureRandom
import java.security.spec.PKCS8EncodedKeySpec
import javax.crypto.AEADBadTagException
import javax.crypto.Cipher
import javax.crypto.KeyAgreement
import javax.crypto.spec.IvParameterSpec
import javax.crypto.spec.SecretKeySpec

/**
 * Runs on the phone (am instrument; not connectedAndroidTest, which uninstalls the app and drops
 * the notification-access grant). Uses test aliases only; the real phone key is not touched.
 * Logs only modes and security levels, never key material.
 */
@RunWith(AndroidJUnit4::class)
class CryptoDeviceTest {
    private val ctx = InstrumentationRegistry.getInstrumentation().targetContext
    private val keystoreVault = KeyVault(ctx, alias = "test_keystore_x25519")
    private val wrappedVault = KeyVault(ctx, alias = "test_wrapped_x25519", allowKeystore = false)

    private fun hex(s: String) = ByteArray(s.length / 2) { s.substring(it * 2, it * 2 + 2).toInt(16).toByte() }

    @After fun cleanup() {
        keystoreVault.delete()
        wrappedVault.delete()
    }

    private fun checkAgreement(vault: KeyVault) {
        val peer = KeyPairGenerator.getInstance("XDH").generateKeyPair()
        val ours = vault.agree(KeyVault.rawFromX509(peer.public.encoded))
        val ka = KeyAgreement.getInstance("XDH")
        ka.init(peer.private)
        ka.doPhase(KeyVault.publicFromRaw(vault.publicKey()), true)
        assertArrayEquals(ka.generateSecret(), ours)
        assertEquals(32, vault.publicKey().size)
    }

    @Test fun keystoreX25519() {
        val mode = keystoreVault.ensure()
        Log.i(TAG, "keystore vault: mode=$mode level=${keystoreVault.securityLevel()} failure=${keystoreVault.keystoreFailure}")
        checkAgreement(keystoreVault)
        // Same key on a second open.
        assertArrayEquals(keystoreVault.publicKey(), KeyVault(ctx, alias = "test_keystore_x25519").publicKey())
    }

    @Test fun wrappedX25519() {
        assertEquals(KeyVault.Mode.WRAPPED, wrappedVault.ensure())
        Log.i(TAG, "wrapped vault: level=${wrappedVault.securityLevel()}")
        checkAgreement(wrappedVault)
    }

    @Test fun rejectsLowOrderPeer() {
        wrappedVault.ensure()
        try {
            wrappedVault.agree(ByteArray(32)) // the all-zero point gives an all-zero secret
            fail("expected rejection")
        } catch (_: Exception) {
        }
    }

    /** RFC 7748 section 6.1, through the raw-key conversions. */
    @Test fun rfc7748Vector() {
        val pkcs8Prefix = hex("302e020100300506032b656e04220420")
        val alicePriv = KeyFactory.getInstance("XDH").generatePrivate(
            PKCS8EncodedKeySpec(pkcs8Prefix + hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a")),
        )
        val ka = KeyAgreement.getInstance("XDH")
        ka.init(alicePriv)
        ka.doPhase(KeyVault.publicFromRaw(hex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f")), true)
        assertArrayEquals(hex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742"), ka.generateSecret())
    }

    @Test fun chacha20Poly1305() {
        val key = ByteArray(32).also { SecureRandom().nextBytes(it) }
        val nonce = ByteArray(12).also { SecureRandom().nextBytes(it) }
        val aad = "header".toByteArray()
        val msg = "test payload".toByteArray()
        val c = Cipher.getInstance("ChaCha20/Poly1305/NoPadding")
        c.init(Cipher.ENCRYPT_MODE, SecretKeySpec(key, "ChaCha20"), IvParameterSpec(nonce))
        c.updateAAD(aad)
        val ct = c.doFinal(msg)
        assertEquals(msg.size + 16, ct.size)
        Log.i(TAG, "ChaCha20-Poly1305 provider: ${c.provider.name}")

        val d = Cipher.getInstance("ChaCha20/Poly1305/NoPadding")
        d.init(Cipher.DECRYPT_MODE, SecretKeySpec(key, "ChaCha20"), IvParameterSpec(nonce))
        d.updateAAD(aad)
        assertArrayEquals(msg, d.doFinal(ct))

        ct[0] = (ct[0].toInt() xor 1).toByte()
        val e = Cipher.getInstance("ChaCha20/Poly1305/NoPadding")
        e.init(Cipher.DECRYPT_MODE, SecretKeySpec(key, "ChaCha20"), IvParameterSpec(nonce))
        e.updateAAD(aad)
        try {
            e.doFinal(ct)
            fail("tampered ciphertext accepted")
        } catch (_: AEADBadTagException) {
        }
    }

    /** tests/fixtures/companion/vectors.json through Conscrypt (fixed test values, never used for real). */
    @Test fun companionVectorsOnDevice() {
        val ss = hex("6d54cc9c397e31691401110f58da1e182a635d7e44c21dc2d7be93624652ab15")
        val pid = hex("a1a2a3a4a5a6a7a8")
        val keys = nl.paperos.companion.proto.KeySchedule.derive(
            ss, pid,
            hex("8f40c5adb68f25624ae5b214ea767a6ec94d829d3d7b5e1ad1ba6f3e2138285f"),
            hex("79a631eede1bf9c98f12032cdeadd0e7a079398fc786b88cc846ec89af85a51a"),
        )
        assertEquals("929074", keys.code)
        val reply = "AUShoqOkpaanqAAAAAAAAAABICEiIyQlJicoKSorQtFqLcJmOeYlm8-9wYIluPcApKH3g4NUeQ7bApP5MuSup2cBKy-EBjOg_8QtXuZ3n3dr21R9YhRnRT9oc8kOshxkxpjh4gbfyoVfOdU-6XSNXnqhRS64_RoX0SLxZVMqCE1CA-rHhQ"
        val pt = """{"t":"reply","k":"0|com.whatsapp|1|test|10001","text":"See you at six","ts":1768480100}""".toByteArray()
        val E = nl.paperos.companion.proto.Envelope
        assertEquals(reply, E.seal(keys.d2p, E.DIR_D, pid, 1, pt, hex("202122232425262728292a2b")))
        assertArrayEquals(pt, E.open(keys.d2p, E.DIR_D, pid, reply)!!.plaintext)
        assertEquals(null, E.open(keys.p2d, E.DIR_D, pid, reply))
    }

    @Test fun hkdfOnDevice() {
        val out = Hkdf.derive(ByteArray(0), ByteArray(22) { 0x0b }, ByteArray(0), 42)
        assertEquals(42, out.size)
        assertEquals(0x8d.toByte(), out[0])
        assertTrue(out.last() == 0xc8.toByte())
    }

    companion object {
        private const val TAG = "PaperOSCryptoTest"
    }
}
