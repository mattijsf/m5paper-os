package nl.paperos.companion

import android.Manifest
import android.content.ContentUris
import android.content.Context
import android.content.pm.PackageManager
import android.provider.CalendarContract
import nl.paperos.companion.proto.Agenda
import org.json.JSONObject
import java.io.File
import java.time.LocalDate
import java.time.ZoneId

/**
 * The phone's calendar for the Paper (DECISIONS.md D28, docs/COMPANION.md 7.1 `agenda`). One way: the Paper never changes it.
 *
 * Reads the instances of the calendars shown in the calendar app for today and the 13 days after it (declined and cancelled
 * events left out), packs them into `agenda` runs ([Agenda.pack]) and sends a run when one of its days changed since it was
 * last sent, the whole window once a day and after pairing or switching the share on. With the share off (or the permission
 * taken away) one empty run empties the Paper's agenda. Runs on the relay thread ([RelayService]); nothing is logged.
 *
 * Kept in noBackupFilesDir/agenda.json: per day a fingerprint of what was sent (a hash, no titles), the day of the last full
 * send, and whether the Paper holds an agenda.
 */
object AgendaSync {
    private const val DEBOUNCE_MS = 60_000L

    @Volatile private var dueAt = 0L       // a calendar change: send after the debounce
    @Volatile private var forceNext = false // the whole window, on the next tick

    fun permitted(ctx: Context) = ctx.checkSelfPermission(Manifest.permission.READ_CALENDAR) == PackageManager.PERMISSION_GRANTED

    /** The calendar changed (ContentObserver): send what changed in a minute. */
    fun calendarChanged(ctx: Context) {
        dueAt = System.currentTimeMillis() + DEBOUNCE_MS
        RelayService.kick(ctx)
    }

    /** After pairing, or the switch or the permission changed: the whole window (or the clearing run) on the next tick. */
    fun requestFull(ctx: Context) {
        forceNext = true
        RelayService.kick(ctx)
    }

    /** Milliseconds until a debounced send is due, or null. */
    fun msUntilDue(): Long? = if (dueAt == 0L) null else (dueAt - System.currentTimeMillis()).coerceAtLeast(0)

    /** Called from the relay thread on every tick. Returns the number of envelopes queued. */
    @Synchronized
    fun tick(ctx: Context): Int {
        if (Link.stage != Link.Stage.ACTIVE) return 0
        val state = State.load(ctx)
        val today = LocalDate.now()
        val zone = ZoneId.systemDefault()
        val sharing = Link.agendaShared && permitted(ctx)
        if (!sharing) {
            forceNext = false
            dueAt = 0
            if (!state.held) return 0
            if (!Link.send(ctx, Agenda.clear(today))) return 0
            State().save(ctx)
            return 1
        }
        val full = forceNext || state.fullDay != today.toString() || !state.held
        val changed = dueAt != 0L && System.currentTimeMillis() >= dueAt
        if (!full && !changed) return 0
        forceNext = false
        dueAt = 0
        val days = Agenda.days(read(ctx, today, zone) ?: return 0, today, zone)
        val prints = days.associate { it.date.toString() to Agenda.fingerprint(it) }
        var sent = 0
        for (run in Agenda.pack(days)) {
            val inRun = days.filter { !it.date.isBefore(run.from) && !it.date.isAfter(run.to) }
            if (!full && inRun.all { state.prints[it.date.toString()] == prints[it.date.toString()] }) continue
            if (!Link.send(ctx, run.plain)) return sent
            sent++
        }
        State(prints, if (full) today.toString() else state.fullDay, true).save(ctx)
        return sent
    }

    /** The visible calendars' instances in the window, or null without the permission. */
    private fun read(ctx: Context, start: LocalDate, zone: ZoneId): List<Agenda.Instance>? {
        if (!permitted(ctx)) return null
        val begin = start.atStartOfDay(zone).toInstant().toEpochMilli()
        val end = start.plusDays(Agenda.DAYS.toLong()).atStartOfDay(zone).toInstant().toEpochMilli()
        val uri = CalendarContract.Instances.CONTENT_URI.buildUpon().also {
            ContentUris.appendId(it, begin - 86_400_000L) // all-day instances are stored at UTC midnights
            ContentUris.appendId(it, end + 86_400_000L)
        }.build()
        val projection = arrayOf(
            CalendarContract.Instances.BEGIN, CalendarContract.Instances.END, CalendarContract.Instances.ALL_DAY,
            CalendarContract.Instances.TITLE, CalendarContract.Instances.EVENT_LOCATION,
        )
        val selection = "${CalendarContract.Instances.VISIBLE} = 1" +
            " AND (${CalendarContract.Instances.STATUS} IS NULL OR ${CalendarContract.Instances.STATUS} != ${CalendarContract.Events.STATUS_CANCELED})" +
            " AND ${CalendarContract.Instances.SELF_ATTENDEE_STATUS} != ${CalendarContract.Attendees.ATTENDEE_STATUS_DECLINED}"
        val out = mutableListOf<Agenda.Instance>()
        runCatching {
            ctx.contentResolver.query(uri, projection, selection, null, null)?.use { c ->
                while (c.moveToNext()) {
                    out += Agenda.Instance(c.getLong(0), c.getLong(1), c.getInt(2) == 1, c.getString(3) ?: "", c.getString(4) ?: "")
                }
            }
        }.onFailure { return null }
        return out
    }

    private class State(val prints: Map<String, String> = emptyMap(), val fullDay: String = "", val held: Boolean = false) {
        fun save(ctx: Context) {
            val p = JSONObject()
            prints.forEach { (k, v) -> p.put(k, v) }
            val o = JSONObject().put("prints", p).put("full", fullDay).put("held", held)
            val f = file(ctx)
            val tmp = File(f.parentFile, f.name + ".tmp")
            tmp.writeText(o.toString())
            tmp.renameTo(f)
        }

        companion object {
            fun file(ctx: Context) = File(ctx.noBackupFilesDir, "agenda.json")
            fun load(ctx: Context): State = runCatching {
                val o = JSONObject(file(ctx).readText())
                val p = o.optJSONObject("prints") ?: JSONObject()
                State(p.keys().asSequence().associateWith { p.getString(it) }, o.optString("full"), o.optBoolean("held"))
            }.getOrDefault(State())
        }
    }

    /** A new pairing: the new Paper holds nothing yet. */
    fun forget(ctx: Context) {
        State.file(ctx).delete()
        dueAt = 0
    }
}
