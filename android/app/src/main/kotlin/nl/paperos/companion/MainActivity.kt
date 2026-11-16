package nl.paperos.companion

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.app.NotificationManager
import android.content.ComponentName
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.provider.Settings
import android.text.InputType
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast

/** One screen: access, Home Assistant token, pairing, replies, status. Never shows message contents or the token. */
class MainActivity : Activity() {
    private lateinit var access: TextView
    private lateinit var grant: Button
    private lateinit var battery: Button
    private lateinit var tokenState: TextView
    private lateinit var tokenInput: EditText
    private lateinit var tokenSave: Button
    private lateinit var tokenClear: Button
    private lateinit var pairState: TextView
    private lateinit var code: TextView
    private lateinit var pair: Button
    private lateinit var confirm: Button
    private lateinit var unpair: Button
    private lateinit var replies: CheckBox
    private lateinit var counts: TextView
    private lateinit var relay: TextView
    private val handler = Handler(Looper.getMainLooper())
    private val refresh: () -> Unit = { runOnUiThread { update() } }
    private val ticker = object : Runnable {
        override fun run() { update(); handler.postDelayed(this, 2000) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Link.load(this)
        val pad = (16 * resources.displayMetrics.density).toInt()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad * 3, pad, pad * 2)
        }
        fun <T : View> add(v: T) = v.also { col.addView(it, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)) }
        fun text(size: Float) = add(TextView(this).apply { textSize = size; setPadding(0, pad / 2, 0, pad / 2) })
        fun button(label: String, onClick: () -> Unit) = add(Button(this).apply { text = label; setOnClickListener { onClick() } })

        text(24f).text = "PaperOS Companion"

        access = text(16f)
        grant = button("Open notification access") { openAccessSettings() }
        button("App info (Allow restricted settings)") {
            startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:$packageName")))
        }
        battery = button("Let it run in the background") {
            @Suppress("BatteryLife")
            startActivity(Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:$packageName")))
        }

        text(18f).text = "Home Assistant"
        tokenState = text(14f)
        tokenInput = add(EditText(this).apply {
            hint = "Paste the long-lived token of the PaperOS phone user"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
            isSingleLine = true
        })
        tokenSave = button("Save token") { saveToken() }
        tokenClear = button("Remove token") { TokenStore.clear(this); update() }

        text(18f).text = "Paper"
        pairState = text(14f)
        code = text(34f)
        pair = button("Pair with Paper (scan QR)") {
            if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 2)
            }
            @Suppress("DEPRECATION")
            startActivityForResult(Intent(this, ScanActivity::class.java), REQ_SCAN)
        }
        confirm = button("The codes match") { Link.confirmCode(this); update() }
        unpair = button("Unpair") {
            AlertDialog.Builder(this).setMessage("Forget the pairing with the Paper?")
                .setPositiveButton("Unpair") { _, _ -> Link.forget(this, notifyPaper = true); update() }
                .setNegativeButton("Cancel", null).show()
        }
        replies = add(CheckBox(this).apply {
            text = "Allow replies from the Paper (sent through the WhatsApp notification)"
            setOnCheckedChangeListener { _, on -> if (on != Link.repliesAllowed) Link.setRepliesAllowed(this@MainActivity, on) }
        })

        text(18f).text = "Status"
        counts = text(14f)
        relay = text(14f)
        button("Clear local counts") { Store.clear(this) }

        setContentView(ScrollView(this).apply { addView(col) })
        RelayService.kick(this)
    }

    @Deprecated("framework Activity result API")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQ_SCAN || resultCode != RESULT_OK) return
        val qr = data?.getStringExtra(ScanActivity.EXTRA_TEXT) ?: return
        Thread {
            val msg = runCatching { Link.startPairing(applicationContext, qr); null }.getOrElse { it.message ?: it.javaClass.simpleName }
            runOnUiThread {
                msg?.let { Toast.makeText(this, it, Toast.LENGTH_LONG).show() }
                update()
            }
        }.start()
    }

    override fun onResume() {
        super.onResume()
        Store.observe(refresh)
        handler.post(ticker)
    }

    override fun onPause() {
        Store.unobserve(refresh)
        handler.removeCallbacks(ticker)
        super.onPause()
    }

    private fun saveToken() {
        val t = tokenInput.text.toString()
        val ok = runCatching { TokenStore.save(this, t) }.isSuccess
        tokenInput.setText("")
        Toast.makeText(this, if (ok) "Token saved" else "That does not look like a token", Toast.LENGTH_SHORT).show()
        RelayService.kick(this)
        update()
    }

    private fun update() {
        val granted = getSystemService(NotificationManager::class.java)
            .isNotificationListenerAccessGranted(ComponentName(this, CaptureListener::class.java))
        access.text = "Notification access: " + (if (granted) "granted" else "NOT granted") +
            "\nListener: " + (if (Store.listenerConnected) "connected" else "not connected")
        grant.text = if (granted) "Notification access settings" else "Grant notification access"
        val unrestricted = getSystemService(PowerManager::class.java).isIgnoringBatteryOptimizations(packageName)
        battery.visibility = if (unrestricted) View.GONE else View.VISIBLE

        val hasToken = TokenStore.has(this)
        tokenState.text = if (hasToken) "Token: saved (encrypted on this phone)" else "Token: not set. Create a non-admin user in Home Assistant, make a long-lived token for it and paste it here."
        tokenInput.visibility = if (hasToken) View.GONE else View.VISIBLE
        tokenSave.visibility = tokenInput.visibility
        tokenClear.visibility = if (hasToken) View.VISIBLE else View.GONE

        val stage = Link.stage
        pairState.text = when {
            stage == Link.Stage.NONE -> "Not paired. On the Paper: Settings > Phone > Pair phone, then scan its code here."
            stage == Link.Stage.ACTIVE -> "Paired (${Link.pidText()}). Notifications go to the Paper encrypted."
            Link.pairingExpired() -> "Pairing timed out. Scan the code again."
            !Link.userConfirmed -> "Check that the Paper shows the same code (pairing id ${Link.pidText()}):"
            else -> "Waiting for the Paper to confirm…"
        }
        code.text = Link.code() ?: ""
        code.visibility = if (stage == Link.Stage.PENDING) View.VISIBLE else View.GONE
        pair.visibility = if (stage == Link.Stage.ACTIVE) View.GONE else View.VISIBLE
        pair.isEnabled = hasToken
        confirm.visibility = if (stage == Link.Stage.PENDING && !Link.userConfirmed) View.VISIBLE else View.GONE
        unpair.visibility = if (stage == Link.Stage.NONE) View.GONE else View.VISIBLE
        unpair.text = if (stage == Link.Stage.PENDING) "Cancel pairing" else "Unpair"
        replies.isChecked = Link.repliesAllowed
        replies.visibility = if (stage == Link.Stage.ACTIVE) View.VISIBLE else View.GONE

        counts.text = buildString {
            append("Notifications (posted / updated / removed / live):")
            Store.counts(this@MainActivity).forEach { (app, c) ->
                append("\n  ").append(Store.APPS[app]).append(": ${c.posted} / ${c.updated} / ${c.removed} / ${c.held}")
            }
        }
        relay.text = "Relay: " + (RelayService.lastStatus?.let(::describe) ?: "not running")
    }

    private fun describe(json: String): String = runCatching {
        val o = org.json.JSONObject(json)
        "waiting ${o.optInt("outbox")}, sent ${o.optInt("pushed")}, received ${o.optInt("accepted")}" +
            ", last push ${o.optInt("last_push")}, last read ${o.optInt("last_poll")}" +
            (o.optString("error").takeIf { it.isNotEmpty() }?.let { ", error $it" } ?: "")
    }.getOrDefault("?")

    private fun openAccessSettings() {
        val detail = Intent(Settings.ACTION_NOTIFICATION_LISTENER_DETAIL_SETTINGS).putExtra(
            Settings.EXTRA_NOTIFICATION_LISTENER_COMPONENT_NAME,
            ComponentName(this, CaptureListener::class.java).flattenToString(),
        )
        runCatching { startActivity(detail) }.onFailure { startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)) }
    }

    companion object {
        private const val REQ_SCAN = 1
    }
}
