package nl.paperos.companion

import android.app.PendingIntent
import android.content.Intent
import android.os.Build
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService

/** Quick Settings tile "Clipboard to Paper" (D23): opens [ClipToPaperActivity], which reads the clipboard with focus. */
class ClipTileService : TileService() {
    override fun onStartListening() {
        Link.load(this)
        qsTile?.apply {
            state = if (Link.clipboardShared) Tile.STATE_INACTIVE else Tile.STATE_UNAVAILABLE
            updateTile()
        }
    }

    override fun onClick() {
        Link.load(this)
        val intent = Intent(this, ClipToPaperActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        if (Build.VERSION.SDK_INT >= 34) {
            startActivityAndCollapse(PendingIntent.getActivity(this, 0, intent, PendingIntent.FLAG_IMMUTABLE))
        } else {
            @Suppress("DEPRECATION", "StartActivityAndCollapseDeprecated")
            startActivityAndCollapse(intent)
        }
    }
}
