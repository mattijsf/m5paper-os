package nl.paperos.companion

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class OutboxTest {
    @get:Rule val tmp = TemporaryFolder()

    private fun box(max: Int = 1000) = Outbox(tmp.root.resolve("outbox"), maxItems = max, clock = { 1_000L })

    @Test fun fifoEvenWithStoppedClock() {
        val b = box()
        (1..5).forEach { b.add(byteArrayOf(it.toByte())) }
        val seen = mutableListOf<Byte>()
        while (true) {
            val f = b.oldest() ?: break
            seen += b.read(f)[0]
            b.remove(f)
        }
        assertEquals(listOf<Byte>(1, 2, 3, 4, 5), seen)
    }

    @Test fun orderSurvivesReopen() {
        box().add(byteArrayOf(1))
        val b = box() // new instance, same dir, same clock
        b.add(byteArrayOf(2))
        assertArrayEquals(byteArrayOf(1), b.read(b.oldest()!!))
    }

    @Test fun dropsOldestOverCap() {
        val b = box(max = 3)
        var dropped = 0
        (1..5).forEach { dropped += b.add(byteArrayOf(it.toByte())) }
        assertEquals(2, dropped)
        assertEquals(3, b.size())
        assertArrayEquals(byteArrayOf(3), b.read(b.oldest()!!))
    }

    @Test(expected = IllegalArgumentException::class) fun rejectsEmpty() {
        box().add(ByteArray(0))
    }

    @Test fun drainSendsAllThenEmpty() {
        val b = box()
        (1..3).forEach { b.add(byteArrayOf(it.toByte())) }
        val got = mutableListOf<Byte>()
        val r = Drain.run(b, { got += it[0]; Relay.Outcome.SENT })
        assertEquals(Drain.Report(Drain.Result.EMPTY, 3, 0), r)
        assertEquals(listOf<Byte>(1, 2, 3), got)
        assertNull(b.oldest())
    }

    @Test fun drainStopsOnRetryAndKeepsItem() {
        val b = box()
        (1..3).forEach { b.add(byteArrayOf(it.toByte())) }
        var calls = 0
        val r = Drain.run(b, { if (++calls == 2) Relay.Outcome.RETRY else Relay.Outcome.SENT })
        assertEquals(Drain.Result.RETRY_LATER, r.result)
        assertEquals(2, b.size())
        assertArrayEquals(byteArrayOf(2), b.read(b.oldest()!!))
    }

    @Test fun drainTreatsExceptionAsRetry() {
        val b = box()
        b.add(byteArrayOf(1))
        val r = Drain.run(b, { error("network down") })
        assertEquals(Drain.Result.RETRY_LATER, r.result)
        assertEquals(1, b.size())
    }

    @Test fun drainDropsRejected() {
        val b = box()
        (1..2).forEach { b.add(byteArrayOf(it.toByte())) }
        val r = Drain.run(b, { if (it[0] == 1.toByte()) Relay.Outcome.REJECTED else Relay.Outcome.SENT })
        assertEquals(Drain.Report(Drain.Result.EMPTY, 1, 1), r)
    }

    @Test fun drainNotConfiguredKeepsAll() {
        val b = box()
        (1..2).forEach { b.add(byteArrayOf(it.toByte())) }
        assertEquals(Drain.Result.NOT_CONFIGURED, Drain.run(b, Relay.NOT_CONFIGURED).result)
        assertEquals(2, b.size())
    }
}
