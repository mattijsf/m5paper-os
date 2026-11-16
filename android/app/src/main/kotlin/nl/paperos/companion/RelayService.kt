package nl.paperos.companion

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.ConnectivityManager
import android.net.Network
import android.database.ContentObserver
import android.os.Handler
import android.os.Looper
import android.provider.CalendarContract
import android.os.IBinder
import org.json.JSONObject
import java.io.File
import java.io.IOException

/**
 * Foreground service (type remoteMessaging) that moves envelopes between the phone and the relay:
 * drains the [Outbox] in counter order with back-off (30 s doubling to 15 min), reads to_phone
 * (every 3 s while pairing, every minute when paired), sends hourly syncs.
 * Writes files/status.json with numbers only, for checks over adb.
 */
class RelayService : Service() {
    private val lock = Object()
    @Volatile private var running = false
    @Volatile private var kicked = true
    private var thread: Thread? = null
    private var netCallback: ConnectivityManager.NetworkCallback? = null
    private var calendarObserver: ContentObserver? = null

    private var backoffMs = BACKOFF_MIN
    private var nextPushAt = 0L
    private var nextPollAt = 0L
    private val st = Status()

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        instance = this
        startInForeground()
        Link.load(this)
        running = true
        thread = Thread(::loop, "paperos-relay").also { it.start() }
        val cm = getSystemService(ConnectivityManager::class.java)
        netCallback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                backoffMs = BACKOFF_MIN
                nextPushAt = 0
                nextPollAt = 0
                Link.sendSync(this@RelayService)
                wake()
            }
        }.also { runCatching { cm.registerDefaultNetworkCallback(it) } }
        // The agenda (D28): a calendar change is sent a minute later (AgendaSync debounces). Registering needs no permission;
        // reading does, and AgendaSync checks it.
        calendarObserver = object : ContentObserver(Handler(Looper.getMainLooper())) {
            override fun onChange(selfChange: Boolean) = AgendaSync.calendarChanged(this@RelayService)
        }.also { runCatching { contentResolver.registerContentObserver(CalendarContract.CONTENT_URI, true, it) } }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        startInForeground()
        wake()
        return START_STICKY
    }

    override fun onDestroy() {
        running = false
        wake()
        netCallback?.let { runCatching { getSystemService(ConnectivityManager::class.java).unregisterNetworkCallback(it) } }
        calendarObserver?.let { runCatching { contentResolver.unregisterContentObserver(it) } }
        instance = null
        super.onDestroy()
    }

    private fun startInForeground() {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, "Relay to PaperOS", NotificationManager.IMPORTANCE_MIN))
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val n = Notification.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_notify_sync_noanim)
            .setContentTitle("PaperOS Companion")
            .setContentText("Relaying notifications to the Paper")
            .setContentIntent(open)
            .setOngoing(true)
            .build()
        startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_REMOTE_MESSAGING)
    }

    private fun wake() = synchronized(lock) { kicked = true; lock.notifyAll() }

    private fun loop() {
        while (running) {
            runCatching { tick() }.onFailure { st.lastError = it.javaClass.simpleName }
            st.write(this)
            val now = System.currentTimeMillis()
            val box = Outbox.of(this)
            var next = nextPollAt
            if (box.size() > 0) next = minOf(next, nextPushAt)
            AgendaSync.msUntilDue()?.let { next = minOf(next, now + it) }
            val waitMs = (next - now).coerceIn(1_000, 60_000)
            synchronized(lock) {
                if (!kicked && running) lock.wait(waitMs)
                kicked = false
            }
        }
    }

    private fun tick() {
        val url = Link.baseUrl
        val token = TokenStore.load(this)
        st.stage = Link.stage.name
        val box = Outbox.of(this)
        st.outbox = box.size()
        if (url == null || token == null) {
            st.state = if (token == null) "no_token" else "no_url"
            nextPollAt = System.currentTimeMillis() + 60_000
            return
        }
        st.state = "ok"
        val ha = HaClient(url, token)
        val now = System.currentTimeMillis()

        if (box.size() > 0 && now >= nextPushAt) {
            val r = Drain.run(box, relayFor(ha)) { !running }
            st.pushed += r.sent
            st.outbox = box.size()
            if (r.result == Drain.Result.RETRY_LATER) {
                nextPushAt = now + backoffMs
                backoffMs = minOf(backoffMs * 2, BACKOFF_MAX)
            } else {
                backoffMs = BACKOFF_MIN
            }
            if (r.sent > 0) checkRelay(ha)
        }

        if (Link.stage != Link.Stage.NONE && now >= nextPollAt) {
            try {
                val entries = ha.read(TO_PHONE)
                st.lastPollCode = if (entries == null) 404 else 200
                if (entries != null) {
                    val rep = Link.receive(this, entries)
                    st.accepted += rep.accepted
                    st.dropped += rep.dropped
                    rep.ackUpto?.let { st.lastAckCode = ha.ack("to_phone", it) }
                }
            } catch (e: HaClient.HttpError) {
                st.lastPollCode = e.code
            } catch (e: IOException) {
                st.lastPollCode = -1
            }
            nextPollAt = now + if (Link.stage == Link.Stage.PENDING) 3_000 else 60_000
        }

        if (Link.stage == Link.Stage.ACTIVE && Link.hoursSinceSync() >= 1.0) Link.sendSync(this)
        // The agenda (D28): changed days, the whole window once a day; queued into the outbox like any envelope.
        if (Link.stage == Link.Stage.ACTIVE) runCatching { st.agendaSent += AgendaSync.tick(this) }
    }

    private fun relayFor(ha: HaClient) = Relay { item ->
        val o = JSONObject(String(item))
        val code = try {
            ha.push(o.getString("q"), o.getLong("id"), o.getString("e"))
        } catch (e: IOException) {
            -1
        }
        st.lastPushCode = code
        when {
            code == 200 -> Relay.Outcome.SENT
            code == 400 -> Relay.Outcome.REJECTED
            else -> Relay.Outcome.RETRY
        }
    }

    /** Diagnostics: shape of what the relay holds now (counts and lengths only, never contents). */
    private fun checkRelay(ha: HaClient) {
        runCatching {
            ha.read(TO_PAPER)?.let { q ->
                st.toPaperCount = q.size
                st.toPaperMaxLen = q.maxOfOrNull { it.e.length } ?: 0
                st.toPaperMaxId = q.maxOfOrNull { it.id } ?: 0
            }
            ha.read(PAIRING)?.let { q ->
                st.pairingCount = q.size
                st.pairingLen = q.firstOrNull()?.e?.length ?: 0
            }
        }
    }

    private class Status {
        var stage = ""
        var state = ""
        var outbox = 0
        var pushed = 0
        var accepted = 0
        var dropped = 0
        var lastPushCode = 0
        var lastPollCode = 0
        var lastAckCode = 0
        var toPaperCount = -1
        var toPaperMaxLen = 0
        var toPaperMaxId = 0L
        var pairingCount = -1
        var pairingLen = 0
        var lastError: String? = null
        var agendaSent = 0

        fun write(ctx: Context) {
            val o = JSONObject().put("stage", stage).put("state", state).put("outbox", outbox).put("pushed", pushed)
                .put("accepted", accepted).put("dropped", dropped).put("last_push", lastPushCode)
                .put("last_poll", lastPollCode).put("last_ack", lastAckCode)
                .put("to_paper", JSONObject().put("n", toPaperCount).put("max_len", toPaperMaxLen).put("max_id", toPaperMaxId))
                .put("pairing", JSONObject().put("n", pairingCount).put("len", pairingLen))
                .put("agenda_sent", agendaSent).put("error", lastError ?: "").put("at", System.currentTimeMillis() / 1000)
            val f = File(ctx.filesDir, "status.json")
            val tmp = File(ctx.filesDir, "status.json.tmp")
            tmp.writeText(o.toString())
            tmp.renameTo(f)
            lastStatus = o.toString()
        }
    }

    companion object {
        private const val CHANNEL = "relay"
        private const val NOTIF_ID = 1
        private const val BACKOFF_MIN = 30_000L
        private const val BACKOFF_MAX = 15 * 60_000L
        const val TO_PHONE = "sensor.paperos_relay_to_phone"
        const val TO_PAPER = "sensor.paperos_relay_to_paper"
        const val PAIRING = "sensor.paperos_relay_pairing"

        @Volatile private var instance: RelayService? = null
        @Volatile var lastStatus: String? = null; private set

        /** Wakes the running service, or starts it. Starting can be refused in the background. */
        fun kick(ctx: Context) {
            instance?.let { it.wake(); return }
            runCatching { ctx.startForegroundService(Intent(ctx, RelayService::class.java)) }
        }
    }
}

/** Starts the relay after a reboot when a token exists. */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        if (intent.action == Intent.ACTION_BOOT_COMPLETED && TokenStore.has(ctx)) RelayService.kick(ctx)
    }
}
