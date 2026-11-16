package nl.paperos.companion.proto

import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.ZoneOffset

/**
 * The `agenda` message (docs/COMPANION.md 7.1, DECISIONS.md D28): the phone's calendar for today and the 13 days after it,
 * one way to the Paper. Plain JVM code: from calendar instances to days, and from days to envelopes that each fit 1000 bytes.
 */
object Agenda {
    const val DAYS = 14
    const val MAX_EVENTS = 12
    const val MAX_TITLE = 80
    const val MAX_LOCATION = 60

    /** One instance as the calendar provider gives it. All-day instances carry UTC midnights (CalendarContract). */
    data class Instance(val begin: Long, val end: Long, val allDay: Boolean, val title: String, val location: String)

    /** One event on one day: minutes after that day's midnight, null when it began before the day / ends after it. */
    data class Ev(val s: Int?, val e: Int?, val n: String, val l: String) {
        val allDay get() = s == null && e == null
    }

    data class Day(val date: LocalDate, val events: List<Ev>, val more: Int)

    /** The window's days ([DAYS] from [start]) with the instances that touch each, in the order the Paper shows them. */
    fun days(instances: List<Instance>, start: LocalDate, zone: ZoneId): List<Day> = (0 until DAYS).map { i ->
        val date = start.plusDays(i.toLong())
        val dayStart = date.atStartOfDay(zone).toInstant().toEpochMilli()
        val dayEnd = date.plusDays(1).atStartOfDay(zone).toInstant().toEpochMilli()
        val evs = instances.mapNotNull { x ->
            if (x.allDay) {
                val first = Instant.ofEpochMilli(x.begin).atZone(ZoneOffset.UTC).toLocalDate()
                var last = Instant.ofEpochMilli(x.end).atZone(ZoneOffset.UTC).toLocalDate().minusDays(1) // end is exclusive
                if (last.isBefore(first)) last = first
                if (date.isBefore(first) || date.isAfter(last)) null else Ev(null, null, cut(x.title, MAX_TITLE), cut(x.location, MAX_LOCATION))
            } else {
                val touches = if (x.end > x.begin) x.begin < dayEnd && x.end > dayStart else x.begin in dayStart until dayEnd
                if (!touches) null else {
                    val s = if (x.begin >= dayStart) ((x.begin - dayStart) / 60_000).toInt().coerceIn(0, 1439) else null
                    val e = if (x.end <= dayEnd) ((x.end - dayStart) / 60_000).toInt().coerceIn(s ?: 0, 1440) else null
                    if (s == null && e == null) Ev(null, null, cut(x.title, MAX_TITLE), cut(x.location, MAX_LOCATION)) // spans the whole day
                    else Ev(s, e, cut(x.title, MAX_TITLE), cut(x.location, MAX_LOCATION))
                }
            }
        }.sortedWith(compareBy<Ev>({ it.s != null }, { !it.allDay }, { it.s ?: 0 }, { it.e ?: 1441 }, { it.n }))
        val kept = evs.take(MAX_EVENTS)
        Day(date, kept, evs.size - kept.size)
    }

    /** One `agenda` plaintext for the days [from]..[to] (all of them must lie in [days]; empty days are left out). */
    fun message(from: LocalDate, to: LocalDate, days: List<Day>): ByteArray {
        val a = JSONArray()
        days.filter { !it.date.isBefore(from) && !it.date.isAfter(to) && (it.events.isNotEmpty() || it.more > 0) }.forEach { d ->
            val ev = JSONArray()
            d.events.forEach { e ->
                val o = JSONObject()
                e.s?.let { o.put("s", it) }
                e.e?.let { o.put("e", it) }
                o.put("n", e.n)
                if (e.l.isNotEmpty()) o.put("l", e.l)
                ev.put(o)
            }
            val o = JSONObject().put("d", (d.date.toEpochDay() - from.toEpochDay()).toInt()).put("ev", ev)
            if (d.more > 0) o.put("more", d.more)
            a.put(o)
        }
        return JSONObject().put("t", "agenda").put("from", from.toString()).put("to", to.toString()).put("days", a)
            .toString().toByteArray(Charsets.UTF_8)
    }

    /** A run of days and its plaintext. */
    class Run(val from: LocalDate, val to: LocalDate, val plain: ByteArray)

    /**
     * Packs the window into as few runs as fit [Envelope.MAX_PLAINTEXT]: each run takes days while they fit. A day that does
     * not fit alone loses its last events to `more` until it does.
     */
    fun pack(days: List<Day>): List<Run> {
        val out = mutableListOf<Run>()
        var i = 0
        while (i < days.size) {
            val from = days[i].date
            var fitted: Day = days[i]
            while (message(from, from, listOf(fitted)).size > Envelope.MAX_PLAINTEXT && fitted.events.isNotEmpty()) {
                fitted = fitted.copy(events = fitted.events.dropLast(1), more = fitted.more + 1)
            }
            val run = mutableListOf(fitted)
            var j = i + 1
            while (j < days.size && message(from, days[j].date, run + days[j]).size <= Envelope.MAX_PLAINTEXT) {
                run += days[j]
                j++
            }
            out += Run(from, run.last().date, message(from, run.last().date, run))
            i = j
        }
        return out
    }

    /** Sharing switched off or the permission taken away: one empty run over the window empties the Paper's agenda. */
    fun clear(start: LocalDate): ByteArray = message(start, start.plusDays((DAYS - 1).toLong()), emptyList())

    /** A short fingerprint of one day as it would be sent: a changed day makes its run go again. */
    fun fingerprint(d: Day): String {
        val md = java.security.MessageDigest.getInstance("SHA-256")
        md.update(message(d.date, d.date, listOf(d)))
        return B64.enc(md.digest().copyOf(12))
    }

    private fun cut(s: String, max: Int) = Messages.cut(s.trim().replace('\n', ' '), max)
}
