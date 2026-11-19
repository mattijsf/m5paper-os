package nl.paperos.companion

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.drawable.ColorDrawable
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import nl.paperos.companion.Ink.Type
import nl.paperos.companion.proto.FirmwarePackage
import nl.paperos.companion.proto.RemoteException
import nl.paperos.companion.proto.RemoteLink
import java.io.IOException
import java.net.ConnectException
import java.net.NoRouteToHostException
import java.net.SocketTimeoutException
import java.text.DateFormat
import java.util.Date

/**
 * Maintenance > Firmware update (DECISIONS.md D40): sends a firmware package that the Mac signed (`make ota-phone` puts it in
 * this phone's Downloads) to the Paper over its wireless development port, as `make ota` does. The phone only carries it:
 * the Paper checks the signature with its own key before it writes a byte, and its hash before it boots it.
 */
class FirmwareActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var packageRow: Ink.Row
    private lateinit var paperRow: Ink.Row
    private lateinit var install: Button
    private lateinit var choose: Button
    private lateinit var result: TextView
    private var pkg: FirmwarePackage? = null
    @Volatile private var paperBuild: String? = null
    @Volatile private var busy = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Ink.load(this)
        Link.load(this)
        window.setBackgroundDrawable(ColorDrawable(Ink.paper))
        val light = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS or WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS
        window.insetsController?.setSystemBarsAppearance(light, light)

        val page = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val (bar, line) = Ink.pageBar(this, "Firmware update")
        status = line
        page.addView(bar, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        val m = Ink.dp(this, 24f)
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(m, Ink.dp(this@FirmwareActivity, 8f), m, Ink.dp(this@FirmwareActivity, 32f))
        }
        fun <T : View> add(v: T, top: Float = 0f) = v.also {
            col.addView(it, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = Ink.dp(this@FirmwareActivity, top) })
        }
        paperRow = add(Ink.Row(this, Ink.ICON_SMARTPHONE, "Paper"))
        packageRow = add(Ink.Row(this, Ink.ICON_FIRMWARE, "Package"))
        packageRow.sub("None chosen")
        choose = add(Ink.button(this, "Choose package", Ink.Kind.SECONDARY) { pick() }, 20f)
        install = add(Ink.button(this, "Install on the Paper") { confirm() }, 12f)
        result = add(Ink.text(this, "", Type.BODY), 20f)
        add(
            Ink.text(
                this,
                "On the Mac, in the PaperOS folder: make ota-phone. It builds and signs the firmware and puts paperos.posota in " +
                    "this phone's Downloads. Only firmware signed with the Paper's key is accepted; the Paper checks it, " +
                    "restarts into it and goes back to the old one if it does not start.",
                Type.CAPTION,
                Ink.secondary,
            ),
            24f,
        )
        page.addView(ScrollView(this).apply { addView(col) }, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
        setContentView(page)
        page.padForSystemBars()
        update()
    }

    override fun onResume() {
        super.onResume()
        if (!busy) askPaper()
    }

    /** What the Paper runs now (its hello), so the user sees whether the package is new. */
    private fun askPaper() {
        val host = Link.remoteHost
        if (host.isEmpty()) {
            paperRow.sub("Address not known yet: open Remote control once")
            return
        }
        paperRow.sub("Asking $host...")
        Thread {
            val key = Link.remoteKey(this)
            val text = if (key == null) {
                "Not paired"
            } else {
                try {
                    RemoteLink.open(host, Link.remotePort, key).use { link -> paperBuild = link.build; "At $host, runs build ${link.build}" }
                } catch (e: IOException) {
                    paperBuild = null
                    reason(e, host)
                } finally {
                    key.fill(0)
                }
            }
            runOnUiThread { paperRow.sub(text); update() }
        }.start()
    }

    private fun pick() {
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        @Suppress("DEPRECATION")
        startActivityForResult(i, REQ_PICK)
    }

    @Deprecated("framework Activity result API")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQ_PICK || resultCode != RESULT_OK) return
        val uri: Uri = data?.data ?: return
        val bytes = runCatching {
            contentResolver.openInputStream(uri)!!.use { s ->
                val b = s.readNBytes(MAX_BYTES + 1)
                check(b.size <= MAX_BYTES)
                b
            }
        }.getOrNull()
        pkg = bytes?.let(FirmwarePackage::parse)
        result.text = if (pkg == null) "That file is not a PaperOS firmware package." else ""
        update()
    }

    private fun update() {
        val p = pkg
        if (p != null) {
            val made = DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(p.madeUnix * 1000))
            val same = if (p.build == paperBuild) " (the Paper runs it already)" else ""
            packageRow.sub("Build ${p.build}$same, ${"%.1f".format(p.imageSize / 1_048_576.0)} MB, signed $made")
        } else {
            packageRow.sub("None chosen")
        }
        install.isEnabled = p != null && !busy
        choose.isEnabled = !busy
    }

    private fun confirm() {
        val p = pkg ?: return
        AlertDialog.Builder(this)
            .setMessage("Install build ${p.build} on the Paper? It restarts into it.")
            .setPositiveButton("Install") { _, _ -> send(p) }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun send(p: FirmwarePackage) {
        val host = Link.remoteHost
        val key = Link.remoteKey(this)
        if (host.isEmpty() || key == null) {
            result.text = if (key == null) "Pair this phone with the Paper first." else "The Paper's address is not known yet: open Remote control once."
            return
        }
        busy = true
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        result.text = "Connecting to $host..."
        update()
        Thread {
            val t0 = System.currentTimeMillis()
            val text = try {
                RemoteLink.open(host, Link.remotePort, key, readTimeoutMs = 30000).use { link ->
                    var shown = -1
                    link.ota(p) { sent, total ->
                        val pct = (sent.toLong() * 100 / total).toInt()
                        if (pct != shown) {
                            shown = pct
                            runOnUiThread { result.text = "Sending: $pct %, ${sent / 1024} of ${total / 1024} KB" }
                        }
                    }
                }
                "Installed in ${(System.currentTimeMillis() - t0) / 1000} s. The Paper restarts into build ${p.build} and keeps " +
                    "it once it has run 15 seconds."
            } catch (e: IOException) {
                if (e is RemoteException) "The Paper refused it: ${e.message}." else reason(e, host)
            } finally {
                key.fill(0)
            }
            runOnUiThread {
                busy = false
                window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                result.text = text
                update()
            }
        }.start()
    }

    private fun reason(e: IOException, host: String): String = when (e) {
        is RemoteException -> if (e.message == "The Paper did not accept this phone") "The Paper did not accept this phone: pair again" else "${e.message}"
        is ConnectException, is NoRouteToHostException, is SocketTimeoutException ->
            "Cannot reach $host: Wireless development off, or another Wi-Fi?"
        else -> "Cannot reach $host: ${e.message ?: e.javaClass.simpleName}"
    }

    companion object {
        private const val REQ_PICK = 1
        private const val MAX_BYTES = 6 * 1024 * 1024
    }
}
