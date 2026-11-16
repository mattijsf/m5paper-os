package nl.paperos.companion

import android.view.View
import android.view.WindowInsets

/**
 * The app targets SDK 36, so Android draws it edge to edge: content would sit under the status bar, the camera cutout and the
 * navigation bar. Pads this view by those insets (and the keyboard's) on top of its own padding.
 */
fun View.padForSystemBars(left: Int = 0, top: Int = 0, right: Int = 0, bottom: Int = 0) {
    setOnApplyWindowInsetsListener { v, insets ->
        val i = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout() or WindowInsets.Type.ime())
        v.setPadding(left + i.left, top + i.top, right + i.right, bottom + i.bottom)
        insets
    }
    requestApplyInsets()
}
