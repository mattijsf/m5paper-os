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

/** One screen: access state, button to the access settings, counts per app. No contents shown. */
class MainActivity : Activity() {
    private lateinit var access: TextView
    private lateinit var counts: TextView
    private lateinit var grant: Button
    private val refresh: () -> Unit = { runOnUiThread { update() } }

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
        text(13f).text = "v0 keeps WhatsApp and Proton Mail notifications on this phone only " +
            "(app-private storage, no backup, no network permission)."
        button("Clear captured items") { Store.clear(this) }

        setContentView(ScrollView(this).apply { addView(col) })
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
            append("Captured (posted since install / held now):")
            Store.counts(this@MainActivity).forEach { (app, c) ->
                append("\n  ").append(Store.APPS[app]).append(": ").append(c.first).append(" / ").append(c.second)
            }
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
}
