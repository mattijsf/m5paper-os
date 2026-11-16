package nl.paperos.companion.proto

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class MessagesTest {
    private fun obj(b: ByteArray?) = JSONObject(String(b!!, Charsets.UTF_8))

    @Test fun postShape() {
        val o = obj(Messages.post("0|com.whatsapp|1|t|1", "whatsapp", "Family", true, 100, listOf(Messages.Msg("Anna", "Hi €5", 99))))
        assertEquals("post", o.getString("t"))
        assertEquals("whatsapp", o.getString("app"))
        assertEquals(true, o.getBoolean("reply"))
        assertEquals("Hi €5", o.getJSONArray("msgs").getJSONObject(0).getString("text"))
    }

    @Test fun mailHasSubject() {
        val o = obj(Messages.post("k", "mail", "", false, 1, listOf(Messages.Msg("Bob", "", 1, subj = "Invoice"))))
        val m = o.getJSONArray("msgs").getJSONObject(0)
        assertEquals("Invoice", m.getString("subj"))
        assertEquals("", m.getString("text"))
    }

    @Test fun fieldLimitsAndFit() {
        val long = "é".repeat(800) // 2 bytes each
        val msgs = (1..9).map { Messages.Msg("S".repeat(100), "$it $long", it.toLong()) }
        val b = Messages.post("k", "whatsapp", "C".repeat(300), true, 1, msgs)!!
        assertTrue(b.size <= Envelope.MAX_PLAINTEXT)
        val o = obj(b)
        assertTrue(o.getString("chat").length <= Messages.MAX_CHAT)
        val a = o.getJSONArray("msgs")
        assertTrue(a.length() in 1..6)
        // Newest message kept.
        assertTrue(a.getJSONObject(a.length() - 1).getString("text").startsWith("9 "))
        assertTrue(a.getJSONObject(0).getString("from").length <= 60)
    }

    @Test fun keepsOnlyLastSix() {
        val b = Messages.post("k", "whatsapp", "", true, 1, (1..8).map { Messages.Msg("a", "m$it", it.toLong()) })
        val a = obj(b).getJSONArray("msgs")
        assertEquals(6, a.length())
        assertEquals("m3", a.getJSONObject(0).getString("text"))
    }

    @Test fun cutKeepsSurrogatePairs() {
        val s = "ab😀cd"
        assertEquals("ab", Messages.cut(s, 3))
        assertEquals("ab😀", Messages.cut(s, 4))
    }

    @Test fun longKeysAreHashed() {
        val short = "0|com.whatsapp|1|x|1"
        assertEquals(short, Messages.wireKey(short))
        val long = "k".repeat(200)
        val w = Messages.wireKey(long)
        assertTrue(w.startsWith("h:") && w.length <= Messages.MAX_KEY)
        assertEquals(w, Messages.wireKey(long))
    }

    @Test fun syncSkippedWhenTooBig() {
        assertEquals("sync", obj(Messages.sync(listOf("a", "b"))).getString("t"))
        assertNull(Messages.sync((1..50).map { "0|com.whatsapp|$it|tag-$it|10123" }))
    }

    @Test fun resultAndParse() {
        val r = obj(Messages.result(5, "gone"))
        assertEquals(5, r.getInt("ref"))
        assertEquals(false, r.getBoolean("ok"))
        assertEquals(true, obj(Messages.result(5, "")).getBoolean("ok"))
        assertTrue(Messages.parse("""{"t":"paired"}""".toByteArray()) is Messages.Incoming.Paired)
        assertTrue(Messages.parse("""{"t":"unpair"}""".toByteArray()) is Messages.Incoming.Unpair)
        assertTrue(Messages.parse("""{"t":"new"}""".toByteArray()) is Messages.Incoming.Other)
        assertTrue(Messages.parse("not json".toByteArray()) is Messages.Incoming.Other)
        assertEquals(listOf("a"), (Messages.parse("""{"t":"dismiss","keys":["a"]}""".toByteArray()) as Messages.Incoming.Dismiss).keys)
    }

    @Test fun guards() {
        val now = 1_000_000L
        fun g(live: Boolean = true, text: String = "ok", envTs: Long = now, entryTs: Long = now, sends: List<Long> = emptyList()) =
            ReplyGuard.check(live, text, envTs, entryTs, now, sends)
        assertEquals("", g())
        assertEquals("gone", g(live = false))
        assertEquals("stale", g(envTs = now - 6 * 3600 - 1))
        assertEquals("stale", g(entryTs = now - 7 * 3600))
        assertEquals("too_long", g(text = "x".repeat(901)))
        assertEquals("", g(text = "x".repeat(900)))
        assertEquals("too_long", g(text = "   "))
        assertEquals("too_soon", g(sends = listOf(now - 5)))
        assertEquals("", g(sends = listOf(now - 10)))
        assertEquals("hourly_limit", g(sends = (1..20).map { now - 60L * it }))
        assertEquals("", g(sends = (1..19).map { now - 60L * it }))
    }

    @Test fun clipBothWays() {
        val o = obj(Messages.clip("Pick up bread\nand milk", 1768480000))
        assertEquals("clip", o.getString("t"))
        assertEquals("Pick up bread\nand milk", o.getString("text"))
        assertEquals(1768480000, o.getLong("ts"))
        assertNull(Messages.clip("   ", 1))
        assertEquals(Messages.MAX_CLIP, obj(Messages.clip("x".repeat(5000), 1)).getString("text").length)
        // Escapes and multi-byte characters: shortened until the plaintext fits.
        val quotes = Messages.clip("\"".repeat(900), 1)!!
        assertTrue(quotes.size <= Envelope.MAX_PLAINTEXT)
        val euros = Messages.clip("€".repeat(900), 1)!!
        assertTrue(euros.size <= Envelope.MAX_PLAINTEXT)
        val parsed = Messages.parse("""{"t":"clip","text":"Somdoku 12:34","ts":5}""".toByteArray())
        assertEquals(Messages.Incoming.Clip("Somdoku 12:34", 5), parsed)
    }
}
