package nl.paperos.companion

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.Gravity
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Scans one QR code with the back camera and returns its text as [EXTRA_TEXT].
 * Frames stay in memory; nothing is saved or logged.
 */
class ScanActivity : ComponentActivity() {
    private lateinit var preview: PreviewView
    private lateinit var hint: TextView
    private val analysis: ExecutorService = Executors.newSingleThreadExecutor()
    private val done = AtomicBoolean(false)

    private val askCamera = registerForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
        if (ok) startCamera() else hint.text = "Camera permission is needed to scan the Paper's QR code."
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        actionBar?.hide()
        val pad = (16 * resources.displayMetrics.density).toInt()
        preview = PreviewView(this)
        hint = TextView(this).apply {
            text = "Point the camera at the QR code on the Paper."
            textSize = 16f
            setPadding(pad, pad, pad, pad)
            setBackgroundColor(0xAA000000.toInt())
            setTextColor(0xFFFFFFFF.toInt())
        }
        val cancel = Button(this).apply {
            text = "Cancel"
            setOnClickListener { finish() }
        }
        val bottom = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(hint, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
            addView(cancel, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
            padForSystemBars()  // the preview fills the screen; the hint and Cancel stay above the navigation bar
        }
        setContentView(FrameLayout(this).apply {
            addView(preview, FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT))
            addView(bottom, FrameLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT, Gravity.BOTTOM))
        })

        if (checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            startCamera()
        } else {
            askCamera.launch(Manifest.permission.CAMERA)
        }
    }

    private fun startCamera() {
        val future = ProcessCameraProvider.getInstance(this)
        future.addListener({
            val provider = future.get()
            val p = Preview.Builder().build().also { it.surfaceProvider = preview.surfaceProvider }
            val a = ImageAnalysis.Builder()
                .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                .build()
            a.setAnalyzer(analysis) { frame -> analyze(frame) }
            provider.unbindAll()
            provider.bindToLifecycle(this, CameraSelector.DEFAULT_BACK_CAMERA, p, a)
        }, mainExecutor)
    }

    private fun analyze(frame: ImageProxy) {
        frame.use {
            if (done.get()) return
            val plane = it.planes[0]
            val buf = plane.buffer
            val y = ByteArray(buf.remaining()).also { b -> buf.get(b) }
            val text = QrDecode.decode(y, plane.rowStride, it.width, it.height) ?: return
            if (done.compareAndSet(false, true)) {
                runOnUiThread {
                    setResult(RESULT_OK, Intent().putExtra(EXTRA_TEXT, text))
                    finish()
                }
            }
        }
    }

    override fun onDestroy() {
        analysis.shutdown()
        super.onDestroy()
    }

    companion object {
        const val EXTRA_TEXT = "qr_text"
    }
}
