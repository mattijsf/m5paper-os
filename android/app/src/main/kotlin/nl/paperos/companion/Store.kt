package nl.paperos.companion

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/** One captured notification. Holds the user's private message text: never log it. */
data class Item(
    val app: String,
    val key: String,
    val time: Long,
    val title: String,
    val text: String,
    val canReply: Boolean,
) {
    fun toJson(): JSONObject = JSONObject()
        .put("app", app).put("key", key).put("time", time)
        .put("title", title).put("text", text).put("canReply", canReply)

    companion object {
        fun fromJson(o: JSONObject) = Item(
            o.getString("app"), o.getString("key"), o.getLong("time"),
            o.optString("title"), o.optString("text"), o.optBoolean("canReply"),
        )
    }
}

/**
 * Captured items, in memory and in app-private storage (filesDir, excluded from backup).
 *
 * items.json holds the latest item per notification key (contents, private).
 * counts.json holds only numbers per app (posted, updated, removed, held), so it can be checked
 * without seeing contents.
 */
object Store {
    /** Apps we keep. Everything else is dropped on arrival. */
    val APPS = mapOf(
        "com.whatsapp" to "WhatsApp",
        "com.whatsapp.w4b" to "WhatsApp Business",
        "ch.protonmail.android" to "Proton Mail",
    )
    private const val MAX_ITEMS = 200

    private val items = LinkedHashMap<String, Item>()
    private val posted = HashMap<String, Int>()
    private val updated = HashMap<String, Int>()
    private val removed = HashMap<String, Int>()
    private var loaded = false
    var listenerConnected = false
        private set

    private val observers = mutableListOf<() -> Unit>()

    @Synchronized
    fun observe(f: () -> Unit) { observers += f }

    @Synchronized
    fun unobserve(f: () -> Unit) { observers -= f }

    @Synchronized
    fun setConnected(ctx: Context, connected: Boolean) {
        load(ctx)
        listenerConnected = connected
        saveCounts(ctx)
        notifyObservers()
    }

    /** Adds or replaces the item for its key. Counts it as posted (new key) or updated. */
    @Synchronized
    fun add(ctx: Context, item: Item) {
        load(ctx)
        val known = items.remove(item.key) != null
        items[item.key] = item
        while (items.size > MAX_ITEMS) items.remove(items.keys.first())
        val m = if (known) updated else posted
        m[item.app] = (m[item.app] ?: 0) + 1
        save(ctx)
        notifyObservers()
    }

    /** The notification was dismissed, read or cancelled on the phone. */
    @Synchronized
    fun remove(ctx: Context, key: String) {
        load(ctx)
        val item = items.remove(key) ?: return
        removed[item.app] = (removed[item.app] ?: 0) + 1
        save(ctx)
        notifyObservers()
    }

    /** After a (re)connect: drop held items whose notification is gone. */
    @Synchronized
    fun retainOnly(ctx: Context, liveKeys: Set<String>) {
        load(ctx)
        val gone = items.keys.filter { it !in liveKeys }
        if (gone.isEmpty()) return
        gone.forEach { k -> items.remove(k)?.let { removed[it.app] = (removed[it.app] ?: 0) + 1 } }
        save(ctx)
        notifyObservers()
    }

    data class Counts(val posted: Int, val updated: Int, val removed: Int, val held: Int)

    /** Per app, since install (or the last Clear). */
    @Synchronized
    fun counts(ctx: Context): Map<String, Counts> {
        load(ctx)
        return APPS.keys.associateWith { app ->
            Counts(posted[app] ?: 0, updated[app] ?: 0, removed[app] ?: 0, items.values.count { it.app == app })
        }
    }

    @Synchronized
    fun clear(ctx: Context) {
        load(ctx)
        items.clear()
        posted.clear()
        updated.clear()
        removed.clear()
        save(ctx)
        notifyObservers()
    }

    private fun notifyObservers() = observers.toList().forEach { it() }

    private fun itemsFile(ctx: Context) = File(ctx.filesDir, "items.json")
    private fun countsFile(ctx: Context) = File(ctx.filesDir, "counts.json")

    private fun load(ctx: Context) {
        if (loaded) return
        loaded = true
        runCatching {
            val a = JSONArray(itemsFile(ctx).readText())
            for (i in 0 until a.length()) Item.fromJson(a.getJSONObject(i)).let { items[it.key] = it }
        }
        runCatching {
            val o = JSONObject(countsFile(ctx).readText())
            for ((name, m) in listOf("posted" to posted, "updated" to updated, "removed" to removed)) {
                val p = o.optJSONObject(name) ?: continue
                p.keys().forEach { m[it] = p.getInt(it) }
            }
        }
    }

    private fun save(ctx: Context) {
        val a = JSONArray()
        items.values.forEach { a.put(it.toJson()) }
        writeAtomic(itemsFile(ctx), a.toString())
        saveCounts(ctx)
    }

    private fun saveCounts(ctx: Context) {
        val o = JSONObject()
        for ((name, m) in listOf("posted" to posted, "updated" to updated, "removed" to removed)) {
            o.put(name, JSONObject().apply { APPS.keys.forEach { put(it, m[it] ?: 0) } })
        }
        o.put("held", JSONObject().apply { APPS.keys.forEach { app -> put(app, items.values.count { it.app == app }) } })
        o.put("connected", listenerConnected)
        writeAtomic(countsFile(ctx), o.toString())
    }

    private fun writeAtomic(f: File, s: String) {
        val tmp = File(f.parentFile, f.name + ".tmp")
        tmp.writeText(s)
        tmp.renameTo(f)
    }
}
