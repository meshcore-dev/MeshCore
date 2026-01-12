package com.meshcore.team.ui.components

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.*
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import timber.log.Timber
import java.util.concurrent.Executors

/**
 * QR Code Scanner using Google ML Kit - much more reliable than ZXing!
 */
@Composable
fun QrCodeScanner(
    onQrCodeScanned: (String) -> Unit,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    
    var hasCameraPermission by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(
                context,
                Manifest.permission.CAMERA
            ) == PackageManager.PERMISSION_GRANTED
        )
    }
    
    val launcher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission()
    ) { granted ->
        hasCameraPermission = granted
        if (!granted) {
            Timber.w("Camera permission denied")
            onDismiss()
        } else {
            Timber.i("Camera permission granted")
        }
    }
    
    LaunchedEffect(Unit) {
        if (!hasCameraPermission) {
            Timber.i("Requesting camera permission...")
            launcher.launch(Manifest.permission.CAMERA)
        }
    }
    
    if (hasCameraPermission) {
        Box(modifier = Modifier.fillMaxSize()) {
            AndroidView(
                factory = { ctx ->
                    Timber.i("Creating CameraX PreviewView for ML Kit scanner...")
                    val previewView = PreviewView(ctx)
                    val cameraProviderFuture = ProcessCameraProvider.getInstance(ctx)
                    
                    cameraProviderFuture.addListener({
                        try {
                            val cameraProvider = cameraProviderFuture.get()
                            
                            // Preview use case
                            val preview = Preview.Builder().build().also {
                                it.setSurfaceProvider(previewView.surfaceProvider)
                            }
                            
                            // Image analysis use case for barcode scanning
                            val barcodeScanner = BarcodeScanning.getClient()
                            val imageAnalyzer = ImageAnalysis.Builder()
                                .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                                .build()
                            
                            var lastScanTime = 0L
                            var scannedCodes = mutableSetOf<String>()
                            
                            imageAnalyzer.setAnalyzer(Executors.newSingleThreadExecutor()) { imageProxy ->
                                val currentTime = System.currentTimeMillis()
                                
                                // Throttle scanning to every 500ms
                                if (currentTime - lastScanTime < 500) {
                                    imageProxy.close()
                                    return@setAnalyzer
                                }
                                
                                @androidx.camera.core.ExperimentalGetImage
                                val mediaImage = imageProxy.image
                                if (mediaImage != null) {
                                    val image = InputImage.fromMediaImage(
                                        mediaImage,
                                        imageProxy.imageInfo.rotationDegrees
                                    )
                                    
                                    barcodeScanner.process(image)
                                        .addOnSuccessListener { barcodes ->
                                            for (barcode in barcodes) {
                                                val rawValue = barcode.rawValue
                                                if (rawValue != null && barcode.valueType == Barcode.TYPE_TEXT) {
                                                    if (!scannedCodes.contains(rawValue)) {
                                                        Timber.i("✓✓✓ QR CODE SCANNED: $rawValue")
                                                        scannedCodes.add(rawValue)
                                                        onQrCodeScanned(rawValue)
                                                    }
                                                }
                                            }
                                            lastScanTime = currentTime
                                        }
                                        .addOnFailureListener { e ->
                                            Timber.e(e, "Barcode scan failed")
                                        }
                                        .addOnCompleteListener {
                                            imageProxy.close()
                                        }
                                } else {
                                    imageProxy.close()
                                }
                            }
                            
                            // Select back camera
                            val cameraSelector = CameraSelector.DEFAULT_BACK_CAMERA
                            
                            // Unbind all use cases before rebinding
                            cameraProvider.unbindAll()
                            
                            // Bind use cases to camera
                            cameraProvider.bindToLifecycle(
                                lifecycleOwner,
                                cameraSelector,
                                preview,
                                imageAnalyzer
                            )
                            
                            Timber.i("✓ ML Kit barcode scanner initialized successfully!")
                            
                        } catch (e: Exception) {
                            Timber.e(e, "Failed to initialize camera")
                        }
                    }, ContextCompat.getMainExecutor(ctx))
                    
                    previewView
                },
                modifier = Modifier.fillMaxSize()
            )
            
            // Overlay with instructions and close button
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .align(Alignment.TopCenter)
                    .padding(16.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Card(
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.9f)
                    )
                ) {
                    Column(
                        modifier = Modifier.padding(16.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Text(
                            text = "Scan QR Code",
                            style = MaterialTheme.typography.titleMedium
                        )
                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = "Point camera at QR code",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
            
            // Cancel button at bottom
            FilledTonalButton(
                onClick = onDismiss,
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .padding(16.dp)
            ) {
                Text("Cancel")
            }
        }
    } else {
        // Permission denied or waiting
        Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center
        ) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text("Camera permission is required to scan QR codes")
                Spacer(modifier = Modifier.height(16.dp))
                Button(onClick = onDismiss) {
                    Text("Close")
                }
            }
        }
    }
}
