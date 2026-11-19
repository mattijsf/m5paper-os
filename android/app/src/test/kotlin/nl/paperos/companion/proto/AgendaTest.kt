package nl.paperos.companion.proto

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.LocalDate
import java.time.ZoneId
import java.time.ZoneOffset

/** docs/COMPANION.md 7.1 `agenda` (D28): instances to days, days to runs that fit an envelope. */
class AgendaTest {
    private val zone = ZoneId.of("Europe/Amsterdam")
    private val thu = LocalDate.of(2026, 1, 15)

    private fun at(d: LocalDate, h: Int, m: Int = 0) = d.atTime(h, m).atZone(zone).toInstant().toEpochMilli()
    private fun utcMidnight(d: LocalDate) = d.atStartOfDay(ZoneOffset.UTC).toInstant().toEpochMilli()
    private fun timed(d: LocalDate, sh: Int, sm: Int, eh: Int, em: Int, t: String, l: String = "") =
        Agenda.Instance(at(d, sh, sm), at(d, eh, em), false, t, l)

    @Test
    fun instancesBecomeDaysInOrder() {
        val days = Agenda.days(
            listOf(
                timed(thu, 13, 0, 14, 0, "Lunch", "Harbour"),
                timed(thu, 9, 30, 10, 15, "Dentist"),
                Agenda.Instance(utcMidnight(thu), utcMidnight(thu.plusDays(1)), true, "Birthday", ""),
                Agenda.Instance(utcMidnight(thu.plusDays(2)), utcMidnight(thu.plusDays(4)), true, "Weekend", "Texel"),
                Agenda.Instance(at(thu.plusDays(1), 22), at(thu.plusDays(2), 7), false, "Night train", ""),
            ),
            thu, zone,
        )
        assertEquals(Agenda.DAYS, days.size)
        assertEquals(listOf("Birthday", "Dentist", "Lunch"), days[0].events.map { it.n })
        assertTrue(days[0].events[0].allDay)
        assertEquals(570, days[0].events[1].s)
        assertEquals(615, days[0].events[1].e)
        assertEquals("Harbour", days[0].events[2].l)
        // Friday: the train leaves at 22:00 and ends after midnight.
        assertEquals(1320, days[1].events.single().s)
        assertNull(days[1].events.single().e)
        // Saturday: the weekend (all day) first, then the rest of the train (until 07:00). Sunday: the weekend.
        assertEquals(listOf("Weekend", "Night train"), days[2].events.map { it.n })
        assertNull(days[2].events[1].s)
        assertEquals(420, days[2].events[1].e)
        assertEquals(listOf("Weekend"), days[3].events.map { it.n })
        assertTrue(days[4].events.isEmpty())
    }

    @Test
    fun moreThanTwelveGoToMore() {
        val many = (0 until 15).map { timed(thu, 8 + it / 2, (it % 2) * 30, 9 + it / 2, 0, "e$it") }
        val d = Agenda.days(many, thu, zone)[0]
        assertEquals(Agenda.MAX_EVENTS, d.events.size)
        assertEquals(3, d.more)
    }

    @Test
    fun messageMatchesTheContract() {
        val days = Agenda.days(listOf(timed(thu.plusDays(2), 10, 5, 10, 25, "Ferry", "Den Helder")), thu, zone)
        val o = JSONObject(String(Agenda.message(thu, thu.plusDays(3), days)))
        assertEquals("agenda", o.getString("t"))
        assertEquals("2026-01-15", o.getString("from"))
        assertEquals("2026-01-18", o.getString("to"))
        val day = o.getJSONArray("days").getJSONObject(0)
        assertEquals(2, day.getInt("d"))
        val ev = day.getJSONArray("ev").getJSONObject(0)
        assertEquals(605, ev.getInt("s"))
        assertEquals(625, ev.getInt("e"))
        assertEquals("Den Helder", ev.getString("l"))
        assertFalse(day.has("more"))
    }

    @Test
    fun packCoversTheWindowWithoutGapsAndEveryRunFits() {
        val long = "x".repeat(80)
        val inst = (0 until Agenda.DAYS).flatMap { d ->
            (0 until 6).map { timed(thu.plusDays(d.toLong()), 8 + it, 0, 9 + it, 0, long, "y".repeat(60)) }
        }
        val days = Agenda.days(inst, thu, zone)
        val runs = Agenda.pack(days)
        assertTrue(runs.size > 1)
        var next = thu
        for (r in runs) {
            assertEquals(next, r.from)
            assertTrue(r.plain.size <= Envelope.MAX_PLAINTEXT)
            next = r.to.plusDays(1)
        }
        assertEquals(thu.plusDays(Agenda.DAYS.toLong()), next)
        // A day that cannot fit alone keeps what fits; the rest is counted.
        val heavy = Agenda.days((0 until 12).map { timed(thu, 8, 0, 9, 0, long, "y".repeat(60)) }, thu, zone)
        val first = Agenda.pack(heavy).first()
        val d0 = JSONObject(String(first.plain)).getJSONArray("days").getJSONObject(0)
        assertTrue(d0.getInt("more") > 0)
        assertEquals(12, d0.getJSONArray("ev").length() + d0.getInt("more"))
    }

    @Test
    fun clearAndFingerprints() {
        val o = JSONObject(String(Agenda.clear(thu)))
        assertEquals("2026-01-28", o.getString("to"))
        assertEquals(0, o.getJSONArray("days").length())
        val a = Agenda.days(listOf(timed(thu, 9, 0, 10, 0, "A")), thu, zone)[0]
        val b = Agenda.days(listOf(timed(thu, 9, 0, 10, 30, "A")), thu, zone)[0]
        assertEquals(Agenda.fingerprint(a), Agenda.fingerprint(a.copy()))
        assertFalse(Agenda.fingerprint(a) == Agenda.fingerprint(b))
    }

    @Test
    fun calendarsSwitchedOffAreLeftOut() {
        val all = Agenda.selection(emptySet())
        assertEquals("visible = 1 AND (eventStatus IS NULL OR eventStatus != 2) AND selfAttendeeStatus != 2", all)
        assertFalse(all.contains("calendar_id"))
        assertEquals(
            "visible = 1 AND calendar_id NOT IN (3,12) AND (eventStatus IS NULL OR eventStatus != 2) AND selfAttendeeStatus != 2",
            Agenda.selection(setOf(12L, 3L)),
        )
    }
}
