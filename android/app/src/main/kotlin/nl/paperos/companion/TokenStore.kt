package nl.paperos.companion

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.io.File
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * The Home Assistant long-lived token of the dedicated non-admin user, pasted by the user.
 * AES-256-GCM with a non-exportable Android Keystore key, in noBackupFilesDir. Never logged or shown.
 */
object TokenStore {
    private const val ALIAS = "paperos_ha_token_aes"
    private fun file(ctx: Context) = File(ctx.noBackupFilesDir, "ha_token.bin")

    fun has(ctx: Context) = file(ctx).exists()

    fun save(ctx: Context, token: String) {
        val t = token.trim()
        require(t.length in 20..1000 && t.none { it.isWhitespace() }) { "token shape" }
        val c = Cipher.getInstance("AES/GCM/NoPadding")
        c.init(Cipher.ENCRYPT_MODE, key())
        val out = c.iv + c.doFinal(t.toByteArray())
        val tmp = File(ctx.noBackupFilesDir, "ha_token.tmp")
        tmp.writeBytes(out)
        check(tmp.renameTo(file(ctx)))
    }

    fun load(ctx: Context): String? = runCatching {
        val b = file(ctx).readBytes()
        val c = Cipher.getInstance("AES/GCM/NoPadding")
        c.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, b, 0, 12))
        String(c.doFinal(b, 12, b.size - 12))
    }.getOrNull()

    fun clear(ctx: Context) {
        file(ctx).delete()
    }

    private fun key(): SecretKey {
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (ks.getKey(ALIAS, null) as? SecretKey)?.let { return it }
        val kg = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        kg.init(
            KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build(),
        )
        return kg.generateKey()
    }
}
