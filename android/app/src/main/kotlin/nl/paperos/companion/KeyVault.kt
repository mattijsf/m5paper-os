package nl.paperos.companion

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyInfo
import android.security.keystore.KeyProperties
import java.io.File
import java.security.KeyFactory
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.PrivateKey
import java.security.PublicKey
import java.security.spec.ECGenParameterSpec
import java.security.spec.PKCS8EncodedKeySpec
import java.security.spec.X509EncodedKeySpec
import javax.crypto.Cipher
import javax.crypto.KeyAgreement
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * The phone's X25519 key pair.
 *
 * Preferred: the private key is generated inside the Android Keystore (TEE or StrongBox) and never
 * leaves it; key agreement runs there. Fallback, when the Keystore cannot do X25519 agreement: a
 * software key whose PKCS#8 bytes are AES-256-GCM encrypted with a Keystore key and kept in
 * noBackupFilesDir. Either way nothing secret is logged and nothing goes to backup.
 */
class KeyVault(
    private val ctx: Context,
    private val alias: String = DEFAULT_ALIAS,
    private val allowKeystore: Boolean = true,
) {
    enum class Mode { NONE, KEYSTORE, WRAPPED }

    private val ks: KeyStore = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
    private val wrapAlias = "$alias.wrap"
    private val wrappedFile get() = File(ctx.noBackupFilesDir, "$alias.wrapped")

    /** Why the Keystore path was not used (exception class only), for diagnostics. */
    var keystoreFailure: String? = null
        private set

    @Synchronized
    fun mode(): Mode = when {
        ks.containsAlias(alias) && ks.entryInstanceOf(alias, KeyStore.PrivateKeyEntry::class.java) -> Mode.KEYSTORE
        wrappedFile.exists() -> Mode.WRAPPED
        else -> Mode.NONE
    }

    /** Creates the key pair if there is none. Returns the mode in use. */
    @Synchronized
    fun ensure(): Mode {
        mode().let { if (it != Mode.NONE) return it }
        if (allowKeystore) {
            try {
                generateInKeystore()
                if (selfTest()) return Mode.KEYSTORE
                keystoreFailure = "agreement self-test mismatch"
            } catch (e: Exception) {
                keystoreFailure = e.javaClass.simpleName
            }
            runCatching { ks.deleteEntry(alias) }
        }
        generateWrapped()
        return Mode.WRAPPED
    }

    /** Raw 32-byte X25519 public key. */
    @Synchronized
    fun publicKey(): ByteArray = when (mode()) {
        Mode.KEYSTORE -> rawFromX509(ks.getCertificate(alias).publicKey.encoded)
        Mode.WRAPPED -> wrappedFile.readBytes().copyOfRange(0, 32)
        Mode.NONE -> error("no key")
    }

    /** X25519(our private, peer public). Rejects the all-zero result (low-order peer key). */
    @Synchronized
    fun agree(peerRaw: ByteArray): ByteArray {
        require(peerRaw.size == 32) { "peer key must be 32 bytes" }
        val peer = publicFromRaw(peerRaw)
        val secret = when (mode()) {
            Mode.KEYSTORE -> {
                val ka = keystoreAgreement()
                ka.init(ks.getKey(alias, null) as PrivateKey)
                ka.doPhase(peer, true)
                ka.generateSecret()
            }
            Mode.WRAPPED -> {
                val ka = KeyAgreement.getInstance(XDH)
                ka.init(unwrapPrivate())
                ka.doPhase(peer, true)
                ka.generateSecret()
            }
            Mode.NONE -> error("no key")
        }
        require(secret.size == 32 && secret.any { it != 0.toByte() }) { "bad shared secret" }
        return secret
    }

    /** "TEE", "StrongBox", "software" or "unknown" for the private key. */
    @Synchronized
    fun securityLevel(): String {
        return runCatching {
            val k = when (mode()) {
                Mode.KEYSTORE -> ks.getKey(alias, null)
                Mode.WRAPPED -> ks.getKey(wrapAlias, null)
                Mode.NONE -> return "none"
            }
            val info = if (k is PrivateKey) {
                KeyFactory.getInstance(k.algorithm, ANDROID_KEYSTORE).getKeySpec(k, KeyInfo::class.java)
            } else {
                val sk = k as SecretKey
                javax.crypto.SecretKeyFactory.getInstance(sk.algorithm, ANDROID_KEYSTORE)
                    .getKeySpec(sk, KeyInfo::class.java) as KeyInfo
            }
            when (info.securityLevel) {
                KeyProperties.SECURITY_LEVEL_STRONGBOX -> "StrongBox"
                KeyProperties.SECURITY_LEVEL_TRUSTED_ENVIRONMENT -> "TEE"
                KeyProperties.SECURITY_LEVEL_SOFTWARE -> "software"
                else -> "unknown"
            }
        }.getOrDefault("unknown")
    }

    @Synchronized
    fun delete() {
        runCatching { ks.deleteEntry(alias) }
        runCatching { ks.deleteEntry(wrapAlias) }
        wrappedFile.delete()
    }

    // ---- Keystore path

    private fun generateInKeystore() {
        val kpg = KeyPairGenerator.getInstance(XDH, ANDROID_KEYSTORE)
        kpg.initialize(
            KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_AGREE_KEY)
                .setAlgorithmParameterSpec(ECGenParameterSpec("x25519"))
                .build(),
        )
        kpg.generateKeyPair()
    }

    private fun keystoreAgreement(): KeyAgreement =
        runCatching { KeyAgreement.getInstance(XDH, ANDROID_KEYSTORE) }.getOrElse { KeyAgreement.getInstance(XDH) }

    /** Agreement with a throwaway software peer must give the same secret on both sides. */
    private fun selfTest(): Boolean {
        val peer = KeyPairGenerator.getInstance(XDH).generateKeyPair()
        val ours = agree(rawFromX509(peer.public.encoded))
        val ka = KeyAgreement.getInstance(XDH)
        ka.init(peer.private)
        ka.doPhase(publicFromRaw(publicKey()), true)
        return ka.generateSecret().contentEquals(ours)
    }

    // ---- Wrapped software path

    private fun generateWrapped() {
        val kp = KeyPairGenerator.getInstance(XDH).generateKeyPair()
        val pkcs8 = kp.private.encoded
        try {
            val c = Cipher.getInstance(AES_GCM)
            c.init(Cipher.ENCRYPT_MODE, wrapKey())
            val ct = c.doFinal(pkcs8)
            val out = rawFromX509(kp.public.encoded) + c.iv + ct
            val tmp = File(wrappedFile.parentFile, wrappedFile.name + ".tmp")
            tmp.writeBytes(out)
            check(tmp.renameTo(wrappedFile)) { "could not store key" }
        } finally {
            pkcs8.fill(0)
        }
    }

    private fun unwrapPrivate(): PrivateKey {
        val blob = wrappedFile.readBytes()
        val iv = blob.copyOfRange(32, 44)
        val c = Cipher.getInstance(AES_GCM)
        c.init(Cipher.DECRYPT_MODE, wrapKey(), GCMParameterSpec(128, iv))
        val pkcs8 = c.doFinal(blob, 44, blob.size - 44)
        try {
            return KeyFactory.getInstance(XDH).generatePrivate(PKCS8EncodedKeySpec(pkcs8))
        } finally {
            pkcs8.fill(0)
        }
    }

    private fun wrapKey(): SecretKey {
        (ks.getKey(wrapAlias, null) as? SecretKey)?.let { return it }
        val kg = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE)
        kg.init(
            KeyGenParameterSpec.Builder(wrapAlias, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build(),
        )
        return kg.generateKey()
    }

    companion object {
        const val DEFAULT_ALIAS = "paperos_phone_x25519"
        private const val ANDROID_KEYSTORE = "AndroidKeyStore"
        private const val XDH = "XDH"
        private const val AES_GCM = "AES/GCM/NoPadding"

        /** DER prefix of an X.509 SubjectPublicKeyInfo for X25519 (RFC 8410). */
        private val X25519_SPKI_PREFIX = byteArrayOf(
            0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x6e, 0x03, 0x21, 0x00,
        )

        fun rawFromX509(encoded: ByteArray): ByteArray {
            require(
                encoded.size == 44 && encoded.copyOfRange(0, 12).contentEquals(X25519_SPKI_PREFIX),
            ) { "not an X25519 public key" }
            return encoded.copyOfRange(12, 44)
        }

        fun publicFromRaw(raw: ByteArray): PublicKey =
            KeyFactory.getInstance(XDH).generatePublic(X509EncodedKeySpec(X25519_SPKI_PREFIX + raw))
    }
}
