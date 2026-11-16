package nl.paperos.companion

import com.google.zxing.BarcodeFormat
import com.google.zxing.BinaryBitmap
import com.google.zxing.DecodeHintType
import com.google.zxing.NotFoundException
import com.google.zxing.PlanarYUVLuminanceSource
import com.google.zxing.ReaderException
import com.google.zxing.common.HybridBinarizer
import com.google.zxing.qrcode.QRCodeReader

/** Decodes a QR code from an 8-bit luminance plane (the Y plane of a camera frame). Pure JVM. */
object QrDecode {
    private val hints = mapOf(
        DecodeHintType.POSSIBLE_FORMATS to listOf(BarcodeFormat.QR_CODE),
        DecodeHintType.TRY_HARDER to true,
    )

    /**
     * [y] holds [height] rows of [rowStride] bytes; only the first [width] bytes of each row are pixels.
     * Returns the text, or null when there is no readable QR code.
     */
    fun decode(y: ByteArray, rowStride: Int, width: Int, height: Int): String? {
        require(rowStride >= width && y.size >= rowStride * (height - 1) + width) { "plane too small" }
        val data = if (y.size >= rowStride * height) y else y.copyOf(rowStride * height)
        val source = PlanarYUVLuminanceSource(data, rowStride, height, 0, 0, width, height, false)
        // A white-on-black code (e-ink in dark mode) decodes from the inverted source.
        for (s in listOf(source, source.invert())) {
            try {
                return QRCodeReader().decode(BinaryBitmap(HybridBinarizer(s)), hints).text
            } catch (_: NotFoundException) {
            } catch (_: ReaderException) {
            }
        }
        return null
    }
}
