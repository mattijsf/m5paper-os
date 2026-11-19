package nl.paperos.companion

import android.app.Activity
import android.graphics.drawable.ColorDrawable
import android.os.Bundle
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowInsetsController
import android.widget.LinearLayout
import android.widget.ScrollView
import nl.paperos.companion.Ink.Type

/**
 * Paper > Calendars (D28): which of the phone's calendars go to the Paper's agenda. One toggle per calendar shown in the
 * calendar app, grouped by account. Every calendar is on unless switched off here, so a calendar added later is shared.
 * A change sends the whole window again (Link.setCalendarShared).
 */
class CalendarsActivity : Activity() {
    private lateinit var list: LinearLayout
    private lateinit var status: android.widget.TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Ink.load(this)
        Link.load(this)
        window.setBackgroundDrawable(ColorDrawable(Ink.paper))
        val light = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS or WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS
        window.insetsController?.setSystemBarsAppearance(light, light)

        val page = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val (bar, line) = Ink.pageBar(this, "Calendars")
        status = line
        page.addView(bar, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        val m = Ink.dp(this, 24f)
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(m, Ink.dp(this@CalendarsActivity, 8f), m, Ink.dp(this@CalendarsActivity, 32f))
        }
        list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        col.addView(list, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        col.addView(
            Ink.text(
                this,
                "The events of the calendars switched on go to the Paper's agenda, read only. A calendar added later is on. " +
                    "Calendars hidden in your calendar app are not listed and not sent.",
                Type.CAPTION,
                Ink.secondary,
            ),
            LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = Ink.dp(this@CalendarsActivity, 24f) },
        )
        page.addView(ScrollView(this).apply { addView(col) }, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
        setContentView(page)
        page.padForSystemBars()
    }

    override fun onResume() {
        super.onResume()
        fill()
    }

    private fun fill() {
        list.removeAllViews()
        val cals = AgendaSync.calendars(this)
        if (cals == null) {
            status.text = "No access to the calendar"
            list.addView(Ink.text(this, "Switch on Share the agenda first: it asks for access to the calendar.", Type.BODY, Ink.secondary))
            return
        }
        if (cals.isEmpty()) {
            status.text = "No calendars"
            list.addView(Ink.text(this, "This phone shows no calendars.", Type.BODY, Ink.secondary))
            return
        }
        var account: String? = null
        for (c in cals) {
            if (c.account != account) {
                account = c.account
                list.addView(
                    Ink.text(this, c.account.ifEmpty { "On this phone" }, Type.CAPTION, Ink.secondary),
                    LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply {
                        topMargin = Ink.dp(this@CalendarsActivity, if (list.childCount == 0) 8f else 24f)
                    },
                )
            }
            val row = Ink.Row(this, Ink.ICON_CALENDAR, c.name.ifEmpty { "Unnamed" })
            row.end = Ink.Toggle(this).apply {
                on = c.id !in Link.agendaSkipped
                onChange = { shared ->
                    Link.setCalendarShared(this@CalendarsActivity, c.id, shared)
                    count(cals)
                }
            }
            list.addView(row, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        }
        count(cals)
    }

    private fun count(cals: List<nl.paperos.companion.proto.Agenda.Calendar>) {
        status.text = shareText(cals.count { it.id !in Link.agendaSkipped }, cals.size)
        status.visibility = View.VISIBLE
    }

    companion object {
        /** "All 4 calendars to the Paper" / "3 of 4 calendars to the Paper" / "No calendar to the Paper". */
        fun shareText(on: Int, total: Int): String = when {
            on == 0 -> "No calendar to the Paper"
            on == total -> if (total == 1) "The calendar goes to the Paper" else "All $total calendars to the Paper"
            else -> "$on of $total calendars to the Paper"
        }
    }
}
