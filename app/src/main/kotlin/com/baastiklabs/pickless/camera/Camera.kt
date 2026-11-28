package com.baastiklabs.pickless.camera

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.camera.core.ExperimentalGetImage
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageCapture
import androidx.camera.core.ImageCaptureException
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.core.UseCase
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.google.mlkit.vision.barcode.BarcodeScannerOptions
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.text.TextRecognition
import com.google.mlkit.vision.text.latin.TextRecognizerOptions
import java.util.concurrent.Executors
import kotlin.coroutines.resume
import kotlin.coroutines.suspendCoroutine

@Composable
fun rememberCameraPermission(): Pair<Boolean, () -> Unit> {
    val ctx = LocalContext.current
    var granted by remember { mutableStateOf(ContextCompat.checkSelfPermission(ctx, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) }
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted = it }
    LaunchedEffect(Unit) { if (!granted) launcher.launch(Manifest.permission.CAMERA) }
    return granted to { launcher.launch(Manifest.permission.CAMERA) }
}

/** Camera preview bound to the screen's lifecycle, with optional frame analysis and still capture. */
@Composable
fun CameraView(modifier: Modifier, analysis: ImageAnalysis?, capture: ImageCapture?) {
    val ctx = LocalContext.current
    val owner = LocalLifecycleOwner.current
    AndroidView(
        factory = { c ->
            val view = PreviewView(c).apply { scaleType = PreviewView.ScaleType.FILL_CENTER }
            val future = ProcessCameraProvider.getInstance(c)
            future.addListener({
                runCatching {
                    val provider = future.get()
                    val preview = Preview.Builder().build().also { it.surfaceProvider = view.surfaceProvider }
                    val cases = mutableListOf<UseCase>(preview)
                    analysis?.let { cases += it }
                    capture?.let { cases += it }
                    provider.unbindAll()
                    provider.bindToLifecycle(owner, CameraSelector.DEFAULT_BACK_CAMERA, *cases.toTypedArray())
                }
            }, ContextCompat.getMainExecutor(c))
            view
        },
        modifier = modifier,
    )
    DisposableEffect(Unit) {
        onDispose { runCatching { ProcessCameraProvider.getInstance(ctx).get().unbindAll() } }
    }
}

/** Frame analyzer that reports barcodes (product formats, or QR codes). */
fun barcodeAnalysis(qr: Boolean, onCode: (String) -> Unit): ImageAnalysis {
    val formats = if (qr) intArrayOf(Barcode.FORMAT_QR_CODE) else intArrayOf(Barcode.FORMAT_EAN_13, Barcode.FORMAT_EAN_8, Barcode.FORMAT_UPC_A, Barcode.FORMAT_UPC_E)
    val scanner = BarcodeScanning.getClient(BarcodeScannerOptions.Builder().setBarcodeFormats(formats.first(), *formats.drop(1).toIntArray()).build())
    val analysis = ImageAnalysis.Builder().setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST).build()
    analysis.setAnalyzer(Executors.newSingleThreadExecutor()) { proxy -> analyze(proxy, scanner, onCode) }
    return analysis
}

@androidx.annotation.OptIn(markerClass = [ExperimentalGetImage::class])
private fun analyze(proxy: ImageProxy, scanner: com.google.mlkit.vision.barcode.BarcodeScanner, onCode: (String) -> Unit) {
    val media = proxy.image
    if (media == null) { proxy.close(); return }
    scanner.process(InputImage.fromMediaImage(media, proxy.imageInfo.rotationDegrees))
        .addOnSuccessListener { codes -> codes.firstNotNullOfOrNull { it.rawValue }?.let(onCode) }
        .addOnCompleteListener { proxy.close() }
}

/** Take a still photo and read its text (ingredient labels). */
suspend fun captureText(context: Context, capture: ImageCapture): String? = suspendCoroutine { cont ->
    capture.takePicture(ContextCompat.getMainExecutor(context), object : ImageCapture.OnImageCapturedCallback() {
        @androidx.annotation.OptIn(markerClass = [ExperimentalGetImage::class])
        override fun onCaptureSuccess(image: ImageProxy) {
            val media = image.image
            if (media == null) { image.close(); cont.resume(null); return }
            TextRecognition.getClient(TextRecognizerOptions.DEFAULT_OPTIONS)
                .process(InputImage.fromMediaImage(media, image.imageInfo.rotationDegrees))
                .addOnSuccessListener { cont.resume(it.text) }
                .addOnFailureListener { cont.resume(null) }
                .addOnCompleteListener { image.close() }
        }

        override fun onError(exception: ImageCaptureException) { cont.resume(null) }
    })
}

/** The ingredients part of OCR'd label text (from "Ingredients" to "Contains"/nutrition panel). */
fun ingredientsSection(ocr: String): String {
    val flat = ocr.replace('\n', ' ').replace(Regex("\\s+"), " ")
    val start = Regex("(?i)ingredients?\\s*[:;.]?").find(flat)?.range?.first ?: 0
    var text = flat.substring(start)
    Regex("(?i)\\b(nutrition facts|serving size|distributed by|manufactured (by|for)|keep refrigerated|best (by|before)|www\\.)").find(text)?.let {
        if (it.range.first > 15) text = text.substring(0, it.range.first)
    }
    return text.trim()
}

enum class Buzz { GOOD, CAUTION, AVOID }

/** Single tap = fine, two taps = caution, one long buzz = avoid. */
fun vibrate(context: Context, kind: Buzz) {
    val v = if (Build.VERSION.SDK_INT >= 31) context.getSystemService(VibratorManager::class.java)?.defaultVibrator
    else @Suppress("DEPRECATION") context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
    v ?: return
    val effect = when (kind) {
        Buzz.GOOD -> VibrationEffect.createOneShot(60, VibrationEffect.DEFAULT_AMPLITUDE)
        Buzz.CAUTION -> VibrationEffect.createWaveform(longArrayOf(0, 70, 90, 70), -1)
        Buzz.AVOID -> VibrationEffect.createOneShot(450, VibrationEffect.DEFAULT_AMPLITUDE)
    }
    runCatching { v.vibrate(effect) }
}
