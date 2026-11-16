package nl.paperos.companion

import android.app.Activity
import android.app.NotificationManager
import android.content.ComponentName
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/** One screen: access state, pairing, key and queue state, counts per app. No contents shown. */
class MainActivity : Activity() {
    private lateinit var access: TextView
    private lateinit var keys: TextView
    private lateinit var counts: TextView
    private lateinit var grant: Button
    private val refresh: () -> Unit = { runOnUiThread { update() } }

    @Volatile private var keyLine = "Phone key: …"
    /** Last scanned QR text, in memory only. Pairing uses it once docs/COMPANION.md lands. */
    private var scanned: String? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pad = (16 * resources.displayMetrics.density).toInt()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad * 3, pad, pad)
        }
        fun text(size: Float) = TextView(this).apply {
            textSize = size
            setPadding(0, pad / 2, 0, pad / 2)
        }.also { col.addView(it, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)) }
        fun button(label: String, onClick: () -> Unit) = Button(this).apply {
            text = label
            setOnClickListener { onClick() }
        }.also { col.addView(it, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)) }

        text(24f).text = "PaperOS Companion"
        access = text(18f)
        grant = button("Open notification access") { openAccessSettings() }
        button("App info (Allow restricted settings)") {
            startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:$packageName")))
        }
        counts = text(16f)
        keys = text(16f)
        button("Pair with Paper (scan QR)") {
            @Suppress("DEPRECATION")
            startActivityForResult(Intent(this, ScanActivity::class.java), REQ_SCAN)
        }
        text(13f).text = "Notifications stay on this phone for now (app-private storage, no backup, " +
            "no network permission). Encrypted upload to the Paper comes with pairing."
        button("Clear captured items") { Store.clear(this) }

        setContentView(ScrollView(this).apply { addView(col) })

        Thread {
            keyLine = runCatching {
                val vault = KeyVault(applicationContext)
                val mode = vault.ensure()
                val how = when (mode) {
                    KeyVault.Mode.KEYSTORE -> "in Android Keystore"
                    KeyVault.Mode.WRAPPED -> "software, wrapped by Keystore" +
                        (vault.keystoreFailure?.let { " ($it)" } ?: "")
                    KeyVault.Mode.NONE -> "none"
                }
                "Phone key: X25519 $how, ${vault.securityLevel()}"
            }.getOrElse { "Phone key: failed (${it.javaClass.simpleName})" }
            refresh()
        }.start()
    }

    @Deprecated("framework Activity result API")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQ_SCAN && resultCode == RESULT_OK) {
            scanned = data?.getStringExtra(ScanActivity.EXTRA_TEXT)
            update()
        }
    }

    override fun onResume() {
        super.onResume()
        Store.observe(refresh)
        update()
    }

    override fun onPause() {
        Store.unobserve(refresh)
        super.onPause()
    }

    private fun granted(): Boolean =
        getSystemService(NotificationManager::class.java)
            .isNotificationListenerAccessGranted(ComponentName(this, CaptureListener::class.java))

    private fun update() {
        val ok = granted()
        access.text = buildString {
            append("Notification access: ").append(if (ok) "granted" else "NOT granted")
            append("\nListener: ").append(if (Store.listenerConnected) "connected" else "not connected")
        }
        grant.text = if (ok) "Notification access settings" else "Grant notification access"
        counts.text = buildString {
            append("Captured (posted / updated / removed / held now):")
            Store.counts(this@MainActivity).forEach { (app, c) ->
                append("\n  ").append(Store.APPS[app]).append(": ")
                append("${c.posted} / ${c.updated} / ${c.removed} / ${c.held}")
            }
        }
        keys.text = buildString {
            append(keyLine)
            append("\nUpload queue: ").append(Outbox.of(this@MainActivity).size()).append(" waiting (relay not set up)")
            append("\nPairing: ")
            append(scanned?.let { "QR scanned (${it.length} characters); pairing arrives with the protocol" } ?: "not paired")
        }
    }

    private fun openAccessSettings() {
        val detail = Intent(Settings.ACTION_NOTIFICATION_LISTENER_DETAIL_SETTINGS).putExtra(
            Settings.EXTRA_NOTIFICATION_LISTENER_COMPONENT_NAME,
            ComponentName(this, CaptureListener::class.java).flattenToString(),
        )
        runCatching { startActivity(detail) }
            .onFailure { startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)) }
    }

    companion object {
        private const val REQ_SCAN = 1
    }
}
