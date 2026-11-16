package nl.paperos.companion

import android.content.Context
import java.io.File

/**
 * Durable FIFO of sealed (already encrypted) payloads waiting for upload to the relay.
 * One file per item in noBackupFilesDir/outbox, named by a strictly increasing 20-digit number.
 * Never put plaintext in here.
 */
class Outbox(
    private val dir: File,
    private val maxItems: Int = 1000,
    private val maxItemBytes: Int = 64 * 1024,
    private val clock: () -> Long = System::currentTimeMillis,
) {
    private var last = 0L

    /** Adds one payload. Returns how many of the oldest items were dropped to stay under [maxItems]. */
    @Synchronized
    fun add(sealed: ByteArray): Int {
        require(sealed.isNotEmpty() && sealed.size <= maxItemBytes) { "payload size out of range" }
        dir.mkdirs()
        if (last == 0L) last = files().lastOrNull()?.name?.toLongOrNull() ?: 0L
        last = maxOf(clock() * 1000, last + 1)
        val name = "%020d".format(last)
        val tmp = File(dir, "$name.tmp")
        tmp.writeBytes(sealed)
        check(tmp.renameTo(File(dir, name))) { "could not store outbox item" }
        var dropped = 0
        val all = files()
        for (f in all.take(maxOf(0, all.size - maxItems))) if (f.delete()) dropped++
        return dropped
    }

    @Synchronized
    fun oldest(): File? = files().firstOrNull()

    @Synchronized
    fun size(): Int = files().size

    fun read(item: File): ByteArray = item.readBytes()

    @Synchronized
    fun remove(item: File) {
        item.delete()
    }

    @Synchronized
    fun clear() = files().forEach { it.delete() }

    private fun files(): List<File> =
        (dir.listFiles { f -> f.isFile && f.name.length == 20 && f.name.all(Char::isDigit) } ?: emptyArray())
            .sortedBy { it.name }

    companion object {
        fun of(ctx: Context) = Outbox(File(ctx.noBackupFilesDir, "outbox"))
    }
}

/** Where sealed payloads go. The HA webhook relay implements this once docs/COMPANION.md lands. */
fun interface Relay {
    enum class Outcome { SENT, RETRY, REJECTED, NOT_CONFIGURED }

    fun upload(sealed: ByteArray): Outcome

    companion object {
        /** No relay until pairing exists. */
        val NOT_CONFIGURED = Relay { Outcome.NOT_CONFIGURED }

        fun current(@Suppress("UNUSED_PARAMETER") ctx: Context): Relay = NOT_CONFIGURED
    }
}

/** Uploads oldest first until the outbox is empty or the relay says stop. Pure logic, unit tested. */
object Drain {
    enum class Result { EMPTY, RETRY_LATER, NOT_CONFIGURED, STOPPED }

    data class Report(val result: Result, val sent: Int, val rejected: Int)

    fun run(box: Outbox, relay: Relay, isStopped: () -> Boolean = { false }): Report {
        var sent = 0
        var rejected = 0
        while (true) {
            if (isStopped()) return Report(Result.STOPPED, sent, rejected)
            val item = box.oldest() ?: return Report(Result.EMPTY, sent, rejected)
            val outcome = runCatching { relay.upload(box.read(item)) }.getOrDefault(Relay.Outcome.RETRY)
            when (outcome) {
                Relay.Outcome.SENT -> { box.remove(item); sent++ }
                Relay.Outcome.REJECTED -> { box.remove(item); rejected++ }
                Relay.Outcome.RETRY -> return Report(Result.RETRY_LATER, sent, rejected)
                Relay.Outcome.NOT_CONFIGURED -> return Report(Result.NOT_CONFIGURED, sent, rejected)
            }
        }
    }
}
