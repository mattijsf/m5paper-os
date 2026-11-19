package nl.paperos.companion

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.app.NotificationManager
import android.content.ComponentName
import android.content.Intent
import android.graphics.drawable.ColorDrawable
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.content.pm.PackageManager
import android.provider.Settings
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowInsetsController
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import nl.paperos.companion.Ink.Kind
import nl.paperos.companion.Ink.Type

/**
 * One screen in the PaperOS "Ink" style ([Ink]): this phone's access, the Home Assistant token, the pairing with the Paper and its
 * switches, activity counts. Never shows message contents or the token.
 */
class MainActivity : Activity() {
    private lateinit var accessRow: Ink.Row
    private lateinit var restrictedRow: Ink.Row
    private lateinit var batteryRow: Ink.Row
    private lateinit var tokenRow: Ink.Row
    private lateinit var tokenHint: TextView
    private lateinit var tokenInput: EditText
    private lateinit var tokenSave: Button
    private lateinit var tokenClear: Button
    private lateinit var pairRow: Ink.Row
    private lateinit var pairHint: TextView
    private lateinit var code: TextView
    private lateinit var pair: Button
    private lateinit var confirm: Button
    private lateinit var unpair: Button
    private lateinit var repliesRow: Ink.Row
    private lateinit var replies: Ink.Toggle
    private lateinit var clipRow: Ink.Row
    private lateinit var clipboard: Ink.Toggle
    private lateinit var agendaRow: Ink.Row
    private lateinit var agenda: Ink.Toggle
    private lateinit var sendClipRow: Ink.Row
    private lateinit var remoteRow: Ink.Row
    private lateinit var countsBox: LinearLayout
    private val countRows = mutableMapOf<String, Ink.Row>()
    private lateinit var relayRow: Ink.Row
    private val handler = Handler(Looper.getMainLooper())
    private val refresh: () -> Unit = { runOnUiThread { update() } }
    private val ticker = object : Runnable {
        override fun run() { update(); handler.postDelayed(this, 2000) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        actionBar?.hide() // the page has its own title
        Ink.load(this)
        Link.load(this)
        window.setBackgroundDrawable(ColorDrawable(Ink.paper))
        val light = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS or WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS
        window.insetsController?.setSystemBarsAppearance(light, light) // dark icons on the paper

        val m = Ink.dp(this, 24f) // the Paper's screen margin
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(m, Ink.dp(this@MainActivity, 20f), m, Ink.dp(this@MainActivity, 32f))
        }
        fun <T : View> add(v: T, top: Float = 0f) = v.also {
            col.addView(it, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = Ink.dp(this@MainActivity, top) })
        }
        fun section(title: String) = add(Ink.text(this, title, Type.CAPTION, Ink.secondary), 32f)
        fun row(icon: Int?, title: String) = add(Ink.Row(this, icon, title))
        fun chevron() = Ink.icon(this, Ink.ICON_CHEVRON, sp = 20f)

        add(Ink.text(this, "PaperOS", Type.TITLE))
        add(Ink.text(this, "Companion: this phone's notifications on your Paper, end-to-end encrypted", Type.BODY, Ink.secondary), 6f)

        section("This phone")
        accessRow = row(Ink.ICON_BELL, "Notification access").apply { clickable { openAccessSettings() } }
        restrictedRow = row(Ink.ICON_SHIELD, "Allow restricted settings").apply {
            sub("Android asks for this once, before access can be granted")
            end = chevron()
            clickable { startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:$packageName"))) }
        }
        batteryRow = row(Ink.ICON_BATTERY, "Run in the background").apply {
            sub("Keeps the relay going while the screen is off")
            end = chevron()
            @Suppress("BatteryLife")
            clickable { startActivity(Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:$packageName"))) }
        }

        section("Home Assistant")
        tokenRow = row(Ink.ICON_KEY, "Token")
        tokenHint = add(
            Ink.text(this, "Create a non-admin user in Home Assistant, make a long-lived token for it and paste it here.", Type.BODY, Ink.secondary),
            12f,
        )
        tokenInput = add(Ink.field(this, "Long-lived token", password = true), 12f)
        tokenSave = add(Ink.button(this, "Save token") { saveToken() }, 12f)
        tokenClear = add(Ink.button(this, "Remove token", Kind.SECONDARY) { TokenStore.clear(this); update() }, 16f)

        section("Paper")
        pairRow = row(Ink.ICON_SMARTPHONE, "Not paired")
        pairHint = add(Ink.text(this, "", Type.BODY, Ink.secondary), 12f)
        code = add(Ink.text(this, "", Type.HEADLINE).apply { letterSpacing = 0.08f; textAlignment = View.TEXT_ALIGNMENT_CENTER }, 16f)
        pair = add(Ink.button(this, "Pair with Paper") {
            if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 2)
            }
            @Suppress("DEPRECATION")
            startActivityForResult(Intent(this, ScanActivity::class.java), REQ_SCAN)
        }, 16f)
        confirm = add(Ink.button(this, "The codes match") { Link.confirmCode(this); update() }, 16f)
        repliesRow = row(Ink.ICON_MESSAGE, "Replies from the Paper").apply { sub("Sent through the WhatsApp notification") }
        replies = Ink.Toggle(this).apply { onChange = { on -> Link.setRepliesAllowed(this@MainActivity, on); update() } }
        repliesRow.end = replies
        clipRow = row(Ink.ICON_CLIPBOARD, "Share the clipboard").apply { sub("A copy on the Paper lands on this phone") }
        clipboard = Ink.Toggle(this).apply { onChange = { on -> Link.setClipboardShared(this@MainActivity, on); update() } }
        clipRow.end = clipboard
        agendaRow = row(Ink.ICON_CALENDAR, "Share the agenda").apply { sub("The next two weeks of your calendar, read only") }
        agenda = Ink.Toggle(this).apply {
            onChange = { on ->
                if (on && !AgendaSync.permitted(this@MainActivity)) {
                    requestPermissions(arrayOf(Manifest.permission.READ_CALENDAR), REQ_CALENDAR)
                } else {
                    Link.setAgendaShared(this@MainActivity, on)
                }
                update()
            }
        }
        agendaRow.end = agenda
        sendClipRow = row(Ink.ICON_SEND, "Send clipboard to the Paper").apply {
            sub("Also from Share > Send to Paper, or the Quick Settings tile")
            end = chevron()
            clickable { ClipSync.sendCurrent(this@MainActivity) }
        }
        remoteRow = row(Ink.ICON_REMOTE, "Remote control").apply {
            sub("See and tap the Paper over Wi-Fi")
            end = chevron()
            clickable { startActivity(Intent(this@MainActivity, RemoteActivity::class.java)) }
        }
        unpair = add(Ink.button(this, "Unpair", Kind.SECONDARY) {
            AlertDialog.Builder(this).setMessage("Forget the pairing with the Paper?")
                .setPositiveButton("Unpair") { _, _ -> Link.forget(this, notifyPaper = true); update() }
                .setNegativeButton("Cancel", null).show()
        }, 16f)

        section("Activity")
        countsBox = add(LinearLayout(this).apply { orientation = LinearLayout.VERTICAL })
        relayRow = row(Ink.ICON_REFRESH, "Relay")
        add(Ink.button(this, "Clear local counts", Kind.TEXT) { Store.clear(this) }, 8f)

        setContentView(ScrollView(this).apply {
            setBackgroundColor(Ink.paper)
            addView(col)
            padForSystemBars() // edge to edge (SDK 36): keep the page below the status bar and above the navigation bar
        })
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

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != REQ_CALENDAR) return
        val granted = grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED
        Link.setAgendaShared(this, granted)
        if (!granted) Toast.makeText(this, "The agenda needs access to the calendar", Toast.LENGTH_LONG).show()
        update()
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
        fun show(v: View, on: Boolean) { v.visibility = if (on) View.VISIBLE else View.GONE }
        val granted = getSystemService(NotificationManager::class.java)
            .isNotificationListenerAccessGranted(ComponentName(this, CaptureListener::class.java))
        accessRow.sub(
            when {
                !granted -> "Not granted: tap to open the setting"
                Store.listenerConnected -> "Granted, listening"
                else -> "Granted, the listener is not connected yet"
            },
        )
        if (accessRow.end == null || accessRow.tag != granted) {
            accessRow.end = Ink.icon(this, if (granted) Ink.ICON_CHECK else Ink.ICON_CHEVRON, sp = if (granted) 22f else 20f)
            accessRow.tag = granted
        }
        show(restrictedRow, !granted)
        show(batteryRow, !getSystemService(PowerManager::class.java).isIgnoringBatteryOptimizations(packageName))

        val hasToken = TokenStore.has(this)
        tokenRow.sub(if (hasToken) "Saved, encrypted on this phone" else "Not set")
        if (tokenRow.tag != hasToken) {
            tokenRow.end = if (hasToken) Ink.icon(this, Ink.ICON_CHECK, sp = 22f) else null
            tokenRow.tag = hasToken
        }
        show(tokenHint, !hasToken)
        show(tokenInput, !hasToken)
        show(tokenSave, !hasToken)
        show(tokenClear, hasToken)

        val stage = Link.stage
        val active = stage == Link.Stage.ACTIVE
        pairRow.title.text = when (stage) {
            Link.Stage.NONE -> "Not paired"
            Link.Stage.PENDING -> "Pairing"
            Link.Stage.ACTIVE -> "Paired"
        }
        pairRow.sub(if (stage == Link.Stage.NONE) "" else "Pairing id ${Link.pidText()}")
        pairHint.text = when {
            stage == Link.Stage.NONE && !hasToken -> "Save the Home Assistant token first."
            stage == Link.Stage.NONE -> "On the Paper: Settings > Home Assistant > Phone > Pair phone, then scan its code here."
            active -> "Notifications go to the Paper end-to-end encrypted. Home Assistant only relays them."
            Link.pairingExpired() -> "Pairing timed out. Scan the code again."
            !Link.userConfirmed -> "Check that the Paper shows the same code:"
            else -> "Waiting for the Paper to confirm..."
        }
        code.text = Link.code() ?: ""
        show(code, stage == Link.Stage.PENDING)
        show(pair, !active)
        pair.isEnabled = hasToken
        show(confirm, stage == Link.Stage.PENDING && !Link.userConfirmed)
        show(unpair, stage != Link.Stage.NONE)
        unpair.text = if (stage == Link.Stage.PENDING) "Cancel pairing" else "Unpair"
        replies.on = Link.repliesAllowed
        clipboard.on = Link.clipboardShared
        agenda.on = Link.agendaShared && AgendaSync.permitted(this)
        if (Link.agendaShared && !AgendaSync.permitted(this)) {
            Link.setAgendaShared(this, false) // the permission was taken away in Settings: the Paper's agenda is emptied
            agenda.on = false
        }
        show(agendaRow, active)
        show(repliesRow, active)
        show(clipRow, active)
        show(sendClipRow, active && Link.clipboardShared)
        show(remoteRow, active)
        remoteRow.sub(if (Link.remoteHost.isEmpty()) "See and tap the Paper over Wi-Fi" else "See and tap the Paper at ${Link.remoteHost}")

        Store.counts(this).forEach { (app, c) ->
            val r = countRows.getOrPut(app) {
                Ink.Row(this, if (app.contains("proton")) Ink.ICON_MAIL else Ink.ICON_MESSAGE, Store.APPS[app] ?: app)
                    .also { countsBox.addView(it, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)) }
            }
            r.sub("${c.posted} posted, ${c.updated} updated, ${c.removed} removed, ${c.held} live")
        }
        relayRow.sub(RelayService.lastStatus?.let(::describe) ?: "Not running")
    }

    private fun describe(json: String): String = runCatching {
        val o = org.json.JSONObject(json)
        "${o.optInt("outbox")} waiting, ${o.optInt("pushed")} sent, ${o.optInt("accepted")} received" +
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
        private const val REQ_CALENDAR = 3
    }
}
