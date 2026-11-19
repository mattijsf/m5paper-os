package nl.paperos.companion

import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.StateListDrawable
import android.text.InputType
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView

/**
 * The PaperOS "Ink" design (docs/UI.md) for the phone: the panel's 16 grays, Inter, Lucide line icons, 2 dp strokes, rows with
 * dividers, pill toggles. The grays run from the panel's paper (a warm light gray, measured on photos of the PaperS3) to its ink,
 * so the tokens mean what they mean on the Paper: content in ink, metadata in gray(3), dividers gray(9), quiet fills gray(13).
 * Pressed, selected and on/off use only ink and paper, as on the Paper. No animation.
 */
object Ink {
    private const val PAPER = 0xE4E4DF
    private const val INK = 0x1E1E1E

    /** gray(0) ink .. gray(15) paper, like pos::gray on the Paper. */
    fun gray(n: Int): Int {
        val t = n.coerceIn(0, 15) / 15f
        fun ch(shift: Int): Int {
            val a = (INK shr shift) and 0xFF
            val b = (PAPER shr shift) and 0xFF
            return (a + (b - a) * t + 0.5f).toInt()
        }
        return (0xFF shl 24) or (ch(16) shl 16) or (ch(8) shl 8) or ch(0)
    }

    val ink = gray(0)
    val paper = gray(15)
    val secondary = gray(3)  // metadata only: counts, hints, captions
    val disabled = gray(5)
    val divider = gray(9)
    val fill = gray(13)

    // Lucide code points (assets/fonts/lucide-codepoints.json).
    const val ICON_CHECK = 57452
    const val ICON_X = 57778
    const val ICON_BELL = 57433
    const val ICON_INFO = 57593
    const val ICON_BATTERY = 57427
    const val ICON_KEY = 58531
    const val ICON_SMARTPHONE = 57699
    const val ICON_SCAN = 58870
    const val ICON_MESSAGE = 57622
    const val ICON_CLIPBOARD = 57477
    const val ICON_SEND = 57682
    const val ICON_MAIL = 57615
    const val ICON_REFRESH = 57669
    const val ICON_CHEVRON = 57455
    const val ICON_ALERT = 57463
    const val ICON_SHIELD = 57855
    const val ICON_UNLINK = 57757
    const val ICON_CALENDAR = 57443
    const val ICON_CALENDARS = 59047
    const val ICON_REMOTE = 58634 // tablet-smartphone
    const val ICON_ARROW_LEFT = 57416
    const val ICON_FIRMWARE = 58597 // hard-drive-download

    private var regular: Typeface? = null
    private var strong: Typeface? = null
    private var icons: Typeface? = null

    fun load(ctx: Context) {
        if (regular != null) return
        regular = ctx.resources.getFont(R.font.inter_regular)
        strong = ctx.resources.getFont(R.font.inter_semibold)
        icons = ctx.resources.getFont(R.font.lucide)
    }

    fun dp(ctx: Context, v: Float): Int = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, ctx.resources.displayMetrics).toInt()

    // Type scale: the Paper's caption / body / bodyStrong / title / headline at phone sizes.
    enum class Type(val sp: Float, val strong: Boolean) { CAPTION(13f, false), BODY(16f, false), BODY_STRONG(16f, true), TITLE(22f, true), HEADLINE(34f, true) }

    fun text(ctx: Context, s: CharSequence, type: Type = Type.BODY, color: Int = ink): TextView = TextView(ctx).apply {
        text = s
        setTextSize(TypedValue.COMPLEX_UNIT_SP, type.sp)
        typeface = if (type.strong) strong else regular
        setTextColor(color)
        includeFontPadding = false
        setLineSpacing(0f, 1.2f)
    }

    fun icon(ctx: Context, codePoint: Int, color: Int = ink, sp: Float = 24f): TextView = TextView(ctx).apply {
        text = String(Character.toChars(codePoint))
        typeface = icons
        setTextSize(TypedValue.COMPLEX_UNIT_SP, sp)
        setTextColor(color)
        includeFontPadding = false
        gravity = Gravity.CENTER
    }

    private fun shape(ctx: Context, fill: Int, stroke: Int?): GradientDrawable = GradientDrawable().apply {
        cornerRadius = dp(ctx, 12f).toFloat()
        setColor(fill)
        stroke?.let { setStroke(dp(ctx, 2f), it) }
    }

    enum class Kind { PRIMARY, SECONDARY, TEXT }

    /** A button as on the Paper: primary = ink fill, secondary = ink outline; pressed inverts; disabled = gray outline. */
    fun button(ctx: Context, label: String, kind: Kind = Kind.PRIMARY, onClick: () -> Unit): Button = Button(ctx).apply {
        text = label
        isAllCaps = false
        typeface = strong
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
        stateListAnimator = null
        elevation = 0f
        minHeight = dp(ctx, 52f)
        val normalFill = if (kind == Kind.PRIMARY) ink else paper
        val normalText = if (kind == Kind.PRIMARY) paper else ink
        val bg = StateListDrawable()
        bg.addState(intArrayOf(-android.R.attr.state_enabled), shape(ctx, paper, if (kind == Kind.TEXT) null else disabled))
        bg.addState(intArrayOf(android.R.attr.state_pressed), shape(ctx, if (kind == Kind.PRIMARY) paper else ink, ink))
        bg.addState(intArrayOf(), shape(ctx, normalFill, if (kind == Kind.TEXT) null else ink))
        background = bg
        setTextColor(
            ColorStateList(
                arrayOf(intArrayOf(-android.R.attr.state_enabled), intArrayOf(android.R.attr.state_pressed), intArrayOf()),
                intArrayOf(disabled, if (kind == Kind.PRIMARY) ink else paper, normalText),
            ),
        )
        if (kind == Kind.TEXT) paintFlags = paintFlags or android.graphics.Paint.UNDERLINE_TEXT_FLAG
        setOnClickListener { onClick() }
    }

    /**
     * The bar on top of a sub-page, as the Paper's: a back arrow, the title, a status line under it, a divider below. Returns the
     * bar and the status line.
     */
    fun pageBar(act: android.app.Activity, title: String): Pair<LinearLayout, TextView> {
        val m = dp(act, 16f)
        val outer = LinearLayout(act).apply { orientation = LinearLayout.VERTICAL }
        val bar = LinearLayout(act).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(m / 2, dp(act, 8f), m, dp(act, 8f))
        }
        val back = icon(act, ICON_ARROW_LEFT).apply {
            setPadding(m / 2, m / 2, m / 2, m / 2)
            isClickable = true
            contentDescription = "Back"
            setOnClickListener { act.finish() }
        }
        bar.addView(back, LinearLayout.LayoutParams(dp(act, 48f), dp(act, 48f)))
        val titles = LinearLayout(act).apply { orientation = LinearLayout.VERTICAL }
        titles.addView(text(act, title, Type.BODY_STRONG))
        val status = text(act, "", Type.CAPTION, secondary)
        titles.addView(status, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = dp(act, 2f) })
        bar.addView(titles, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f).apply { marginStart = dp(act, 8f) })
        outer.addView(bar, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        outer.addView(View(act).apply { setBackgroundColor(divider) }, LinearLayout.LayoutParams(MATCH_PARENT, dp(act, 1f)))
        return outer to status
    }

    /** A text field: ink outline, paper inside. */
    fun field(ctx: Context, hint: String, password: Boolean): EditText = EditText(ctx).apply {
        this.hint = hint
        typeface = regular
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
        setTextColor(ink)
        setHintTextColor(secondary)
        isSingleLine = true
        inputType = if (password) InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD else InputType.TYPE_CLASS_TEXT
        typeface = regular // a password input type switches to monospace: back to Inter
        background = shape(ctx, paper, ink)
        val p = dp(ctx, 14f)
        setPadding(p, p, p, p)
    }

    /** The Paper's toggle: on = ink pill, paper knob right; off = ink outline, ink knob left. */
    class Toggle(ctx: Context) : View(ctx) {
        var on = false
            set(v) { field = v; invalidate() }
        var onChange: ((Boolean) -> Unit)? = null
        private val p = Paint(Paint.ANTI_ALIAS_FLAG)
        private val stroke = dp(ctx, 2f).toFloat()

        init {
            isClickable = true
            setOnClickListener { on = !on; onChange?.invoke(on) }
        }

        override fun onMeasure(w: Int, h: Int) = setMeasuredDimension(dp(context, 52f), dp(context, 32f))

        override fun onDraw(c: Canvas) {
            val r = RectF(stroke / 2, stroke / 2, width - stroke / 2, height - stroke / 2)
            val rad = r.height() / 2
            p.style = Paint.Style.FILL
            p.color = if (on) ink else paper
            c.drawRoundRect(r, rad, rad, p)
            p.style = Paint.Style.STROKE
            p.strokeWidth = stroke
            p.color = ink
            c.drawRoundRect(r, rad, rad, p)
            p.style = Paint.Style.FILL
            p.color = if (on) paper else ink
            val knob = rad - dp(context, 5f)
            c.drawCircle(if (on) r.right - rad else r.left + rad, r.centerY(), knob, p)
        }
    }

    /**
     * A list row as on the Paper (ListItem): icon, title (body strong, ink), subtitle (secondary caption, or ink body when it is
     * content), something at the end (a toggle, a check, a chevron), a 1 dp divider under it.
     */
    class Row(ctx: Context, iconCp: Int?, title: String) : LinearLayout(ctx) {
        val icon: TextView? = iconCp?.let { icon(ctx, it) }
        val title: TextView = text(ctx, title, Type.BODY_STRONG)
        val subtitle: TextView = text(ctx, "", Type.CAPTION, secondary)
        private val body = LinearLayout(ctx).apply { orientation = VERTICAL }
        private val line = LinearLayout(ctx).apply { orientation = HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        var end: View? = null
            set(v) {
                field?.let { line.removeView(it) }
                field = v
                v?.let { line.addView(it, LayoutParams(WRAP_CONTENT, WRAP_CONTENT).apply { marginStart = dp(context, 12f) }) }
            }

        init {
            orientation = VERTICAL
            val pad = dp(ctx, 14f)
            line.setPadding(0, pad, 0, pad)
            line.minimumHeight = dp(ctx, 64f)
            icon?.let { line.addView(it, LayoutParams(dp(ctx, 28f), WRAP_CONTENT).apply { marginEnd = dp(ctx, 16f) }) }
            body.addView(this.title, LayoutParams(MATCH_PARENT, WRAP_CONTENT))
            body.addView(subtitle, LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = dp(ctx, 3f) })
            line.addView(body, LayoutParams(0, WRAP_CONTENT, 1f))
            addView(line, LayoutParams(MATCH_PARENT, WRAP_CONTENT))
            addView(View(ctx).apply { setBackgroundColor(divider) }, LayoutParams(MATCH_PARENT, dp(ctx, 1f)))
        }

        fun sub(s: String, content: Boolean = false) {
            subtitle.text = s
            subtitle.visibility = if (s.isEmpty()) GONE else VISIBLE
            subtitle.setTextColor(if (content) ink else secondary)
            subtitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, if (content) 15f else 13f)
        }

        fun clickable(onClick: () -> Unit) {
            line.setOnClickListener { onClick() }
            line.background = StateListDrawable().apply {
                addState(intArrayOf(android.R.attr.state_pressed), GradientDrawable().apply { setColor(fill) })
            }
        }
    }
}
