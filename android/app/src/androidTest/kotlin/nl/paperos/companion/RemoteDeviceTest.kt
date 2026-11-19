package nl.paperos.companion

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import nl.paperos.companion.proto.RemoteAuth
import nl.paperos.companion.proto.RemoteLink
import org.junit.Assert.assertEquals
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Remote control over the real Wi-Fi from the phone (docs/COMPANION.md section 10), against the simulator on the Mac with the
 * test vectors' pairing (a UI test preset with Wireless development on):
 *   am instrument -w -e remoteSim 192.168.x.y:8266 -e class nl.paperos.companion.RemoteDeviceTest nl.paperos.companion.test/...
 * Uses the vectors' key only; the phone's own pairing is not touched.
 */
@RunWith(AndroidJUnit4::class)
class RemoteDeviceTest {
    private fun hex(s: String) = ByteArray(s.length / 2) { s.substring(2 * it, 2 * it + 2).toInt(16).toByte() }

    @Test
    fun screenAndTouchOverWifi() {
        val sim = InstrumentationRegistry.getArguments().getString("remoteSim")
        assumeTrue("pass -e remoteSim host:port", !sim.isNullOrEmpty())
        val (host, port) = sim!!.split(":").let { it[0] to it[1].toInt() }
        val key = RemoteAuth.key(hex("1efe7567b67386fc9d999a840aa50612210dc2e7c88a6349a5b15dcd6d0b20c1"))
        val t0 = System.nanoTime()
        RemoteLink.open(host, port, key).use { link ->
            val signedIn = System.nanoTime()
            val f = link.shot()
            val shot = System.nanoTime()
            assertEquals(259200, f.packed.size)
            link.tap(270, 480)
            link.refreshMark()
            val g = link.shot()
            Log.i("RemoteDeviceTest", "sign-in ${(signedIn - t0) / 1_000_000} ms, frame ${(shot - signedIn) / 1_000_000} ms, " +
                "frames ${f.crc} -> ${g.crc}")
        }
    }
}
