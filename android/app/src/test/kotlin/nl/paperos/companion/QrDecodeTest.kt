package nl.paperos.companion

import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.qrcode.QRCodeWriter
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class QrDecodeTest {
    private val payload = "paperos:pair:v1:" + "A".repeat(43) // shape is illustrative only

    /** Renders [text] as a luminance plane with row padding, like a camera Y plane. */
    private fun plane(text: String, size: Int, rowStride: Int, invert: Boolean = false): ByteArray {
        val m = QRCodeWriter().encode(text, BarcodeFormat.QR_CODE, size, size, mapOf(EncodeHintType.MARGIN to 4))
        val y = ByteArray(rowStride * size) { 0x55 }
        for (r in 0 until size) for (c in 0 until size) {
            val dark = m[c, r] != invert
            y[r * rowStride + c] = if (dark) 20 else 235.toByte()
        }
        return y
    }

    @Test fun decodesPaddedPlane() {
        assertEquals(payload, QrDecode.decode(plane(payload, 480, 512), 512, 480, 480))
    }

    @Test fun decodesShortLastRow() {
        val full = plane(payload, 480, 512)
        val cut = full.copyOf(512 * 479 + 480) // camera buffers may end at the last pixel
        assertEquals(payload, QrDecode.decode(cut, 512, 480, 480))
    }

    @Test fun decodesInverted() {
        assertEquals(payload, QrDecode.decode(plane(payload, 480, 480, invert = true), 480, 480, 480))
    }

    @Test fun nullWithoutCode() {
        assertNull(QrDecode.decode(ByteArray(320 * 240) { 128.toByte() }, 320, 320, 240))
    }
}
