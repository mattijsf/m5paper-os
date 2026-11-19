package nl.paperos.companion

import android.app.Activity
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.drawable.ColorDrawable
import android.os.Bundle
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowInsetsController
import android.view.WindowManager
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import nl.paperos.companion.Ink.Type
import nl.paperos.companion.proto.Frame
import nl.paperos.companion.proto.RemoteAuth
import nl.paperos.companion.proto.RemoteException
import nl.paperos.companion.proto.RemoteLink
import java.io.IOException
import java.net.ConnectException
import java.net.NoRouteToHostException
import java.net.SocketTimeoutException
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/**
 * Remote control of the Paper (docs/COMPANION.md section 10): its screen as the firmware drew it, and this phone's finger as its
 * touch. Tap = tap, hold = long press, drag = swipe. Needs the Paper's Settings > Developer > Wireless development and the same
 * Wi-Fi. One thread owns the connection while the screen is visible: it asks for the refresh log a few times a second and takes
 * the frame when that changed, after every touch and every 5 s anyway (as the Mac's tools/view.py does).
 */
class RemoteActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var screen: PaperView
    private lateinit var setup: LinearLayout
    private lateinit var address: EditText
    private lateinit var hint: TextView
    private val inputs = LinkedBlockingQueue<Touch>()
    @Volatile private var running = false
    @Volatile private var current: RemoteLink? = null // closed from onPause: a read in progress then ends at once
    @Volatile private var wanted = false // connect (the user asked, or an address is known)
    private var worker: Thread? = null

    private sealed class Touch {
        data class Tap(val x: Int, val y: Int) : Touch()
        data class Hold(val x: Int, val y: Int) : Touch()
        data class Swipe(val x1: Int, val y1: Int, val x2: Int, val y2: Int) : Touch()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Ink.load(this)
        Link.load(this)
        window.setBackgroundDrawable(ColorDrawable(Ink.paper))
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val light = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS or WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS
        window.insetsController?.setSystemBarsAppearance(light, light)

        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val (bar, line) = Ink.pageBar(this, "Remote control")
        status = line
        col.addView(bar, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))

        val body = FrameLayout(this)
        screen = PaperView(this).apply { visibility = View.GONE }
        body.addView(screen, FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT))
        setup = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(Ink.dp(this@RemoteActivity, 24f), Ink.dp(this@RemoteActivity, 24f), Ink.dp(this@RemoteActivity, 24f), 0)
        }
        fun <T : View> add(v: T, top: Float = 0f) = v.also {
            setup.addView(it, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = Ink.dp(this@RemoteActivity, top) })
        }
        hint = add(Ink.text(this, "", Type.BODY, Ink.ink))
        add(Ink.text(this, "Address of the Paper", Type.CAPTION, Ink.secondary), 24f)
        address = add(Ink.field(this, "192.168.1.20", password = false), 8f)
        add(Ink.button(this, "Connect") { connectTyped() }, 16f)
        add(
            Ink.text(
                this,
                "On the Paper: Settings > Developer > Wireless development on. It shows its address there and sends it to this " +
                    "phone. Both on the same Wi-Fi.",
                Type.CAPTION,
                Ink.secondary,
            ),
            16f,
        )
        body.addView(setup, FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT))
        col.addView(body, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
        setContentView(col)
        col.padForSystemBars()

        screen.onTouch = { inputs.offer(it) }
        address.setText(Link.remoteHost)
        wanted = Link.remoteHost.isNotEmpty()
        showSetup(if (wanted) "" else "The Paper has not sent its address yet.")
    }

    override fun onResume() {
        super.onResume()
        running = true
        worker = Thread({ loop() }, "remote").also { it.start() }
    }

    override fun onPause() {
        running = false
        current?.close()
        worker?.interrupt()
        worker = null
        super.onPause()
    }

    private fun connectTyped() {
        val host = address.text.toString().trim()
        if (!RemoteAuth.validHost(host)) {
            showSetup("That is not an address.")
            return
        }
        Link.setRemoteAddress(this, host)
        getSystemService(InputMethodManager::class.java).hideSoftInputFromWindow(address.windowToken, 0)
        wanted = true
        worker?.interrupt() // out of its wait: connect now
    }

    private fun showSetup(text: String) = runOnUiThread {
        hint.text = text
        hint.visibility = if (text.isEmpty()) View.GONE else View.VISIBLE
        setup.visibility = View.VISIBLE
        screen.visibility = View.GONE
    }

    private fun say(text: String) = runOnUiThread { status.text = text }

    // ---- the connection (thread "remote")

    private fun loop() {
        while (running) {
            if (!wanted) {
                say("Not connected")
                pause(1000)
                continue
            }
            val key = Link.remoteKey(this)
            if (key == null) {
                say("Not paired")
                showSetup("Pair this phone with the Paper first.")
                wanted = false
                continue
            }
            val host = Link.remoteHost
            say("Connecting to $host...")
            val link = try {
                RemoteLink.open(host, Link.remotePort, key)
            } catch (e: IOException) {
                key.fill(0)
                say("Not connected")
                showSetup(reason(e, host))
                wanted = false
                continue
            }
            key.fill(0)
            current = link
            if (!running) link.close()
            try {
                session(link, host)
            } catch (e: IOException) {
                if (running) {
                    say("Connection lost, connecting again...")
                    pause(1500)
                }
            } finally {
                current = null
                link.close()
            }
        }
    }

    private fun session(link: RemoteLink, host: String) {
        var mark: Int? = null
        var shotAt = 0L
        var shown: String? = null
        while (running && wanted) {
            var sent = false
            var t = try { inputs.poll(POLL_MS, TimeUnit.MILLISECONDS) } catch (_: InterruptedException) { null }
            while (t != null) {
                when (t) {
                    is Touch.Tap -> link.tap(t.x, t.y)
                    is Touch.Hold -> link.longPress(t.x, t.y)
                    is Touch.Swipe -> link.swipe(t.x1, t.y1, t.x2, t.y2)
                }
                sent = true
                t = inputs.poll()
            }
            if (sent) pause(150) // the Paper plays the touch, then draws: ask after that
            val now = link.refreshMark()
            if (sent || now != mark || SystemClock.elapsedRealtime() - shotAt > FULL_EVERY_MS) {
                mark = now
                val f = link.shot()
                shotAt = SystemClock.elapsedRealtime()
                if (f.crc != shown) {
                    shown = f.crc
                    show(f)
                }
            }
            say("Connected to $host")
        }
    }

    private fun show(f: Frame) {
        val bmp = Bitmap.createBitmap(f.width, f.height, Bitmap.Config.ARGB_8888)
        bmp.setPixels(f.pixels(PALETTE), 0, f.width, 0, 0, f.width, f.height)
        runOnUiThread {
            screen.frame = bmp
            setup.visibility = View.GONE
            screen.visibility = View.VISIBLE
        }
    }

    /** Sleeps; false when interrupted (the user asked to connect, or the screen went away). */
    private fun pause(ms: Long): Boolean = try {
        Thread.sleep(ms)
        true
    } catch (_: InterruptedException) {
        false
    }

    private fun reason(e: IOException, host: String): String = when (e) {
        is RemoteException -> if (e.message == "The Paper did not accept this phone") {
            "The Paper did not accept this phone. Pair again (Settings > Home Assistant > Phone on the Paper)."
        } else {
            "The Paper at $host answered strangely: ${e.message}."
        }
        is ConnectException, is NoRouteToHostException, is SocketTimeoutException ->
            "Cannot reach the Paper at $host. Is wireless development on, and is this phone on the same Wi-Fi?"
        else -> "Cannot reach the Paper at $host: ${e.message ?: e.javaClass.simpleName}."
    }

    /** The Paper's frame, scaled to fit, with the finger as its touch. */
    private class PaperView(ctx: Activity) : View(ctx) {
        var frame: Bitmap? = null
            set(v) { field = v; invalidate() }
        var onTouch: ((Touch) -> Unit)? = null
        private val dst = RectF()
        private val paint = Paint(Paint.FILTER_BITMAP_FLAG)
        private val edge = Paint().apply { style = Paint.Style.STROKE; color = Ink.divider; strokeWidth = Ink.dp(ctx, 1f).toFloat() }
        private val slop = ViewConfiguration.get(ctx).scaledTouchSlop
        private var downX = 0f
        private var downY = 0f
        private var downAt = 0L
        private var moved = false

        override fun onDraw(c: Canvas) {
            val b = frame ?: return
            val pad = Ink.dp(context, 8f).toFloat()
            val s = minOf((width - 2 * pad) / b.width, (height - 2 * pad) / b.height)
            val w = b.width * s
            val h = b.height * s
            dst.set((width - w) / 2, (height - h) / 2, (width + w) / 2, (height + h) / 2)
            c.drawBitmap(b, Rect(0, 0, b.width, b.height), dst, paint)
            c.drawRect(dst, edge)
        }

        private fun px(x: Float) = ((x - dst.left) / dst.width() * RemoteLink.WIDTH).toInt()
        private fun py(y: Float) = ((y - dst.top) / dst.height() * RemoteLink.HEIGHT).toInt()

        override fun onTouchEvent(e: MotionEvent): Boolean {
            if (frame == null) return false
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    if (!dst.contains(e.x, e.y)) return false
                    downX = e.x; downY = e.y; downAt = e.eventTime; moved = false
                }
                MotionEvent.ACTION_MOVE -> if (Math.hypot((e.x - downX).toDouble(), (e.y - downY).toDouble()) > slop) moved = true
                MotionEvent.ACTION_UP -> {
                    val t = when {
                        moved -> Touch.Swipe(px(downX), py(downY), px(e.x), py(e.y))
                        e.eventTime - downAt >= HOLD_MS -> Touch.Hold(px(downX), py(downY))
                        else -> Touch.Tap(px(downX), py(downY))
                    }
                    onTouch?.invoke(t)
                    performClick()
                }
            }
            return true
        }

        override fun performClick(): Boolean = super.performClick()
    }

    companion object {
        private const val POLL_MS = 250L
        private const val FULL_EVERY_MS = 5000L
        private const val HOLD_MS = 600L
        /** The Paper's 16 grays as Ink draws them. */
        private val PALETTE = IntArray(16) { Ink.gray(it) }
    }
}
