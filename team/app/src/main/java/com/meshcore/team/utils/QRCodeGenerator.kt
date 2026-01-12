package com.meshcore.team.utils

import android.graphics.Bitmap
import android.graphics.Color
import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.qrcode.QRCodeWriter
import com.google.zxing.qrcode.decoder.ErrorCorrectionLevel
import timber.log.Timber

/**
 * Utility for generating QR codes
 */
object QRCodeGenerator {
    
    /**
     * Generate a QR code bitmap from text
     * @param text Text to encode
     * @param size Size of the QR code in pixels
     * @return Bitmap containing the QR code
     */
    fun generateQRCode(text: String, size: Int = 512): Bitmap {
        Timber.i("🔲 Generating QR Code:")
        Timber.i("  Text: $text")
        Timber.i("  Length: ${text.length} chars")
        Timber.i("  Size: ${size}x${size} pixels")
        
        val hints = hashMapOf<EncodeHintType, Any>().apply {
            put(EncodeHintType.ERROR_CORRECTION, ErrorCorrectionLevel.M)
            put(EncodeHintType.CHARACTER_SET, "UTF-8")
            put(EncodeHintType.MARGIN, 1)
        }
        
        val writer = QRCodeWriter()
        val bitMatrix = writer.encode(text, BarcodeFormat.QR_CODE, size, size, hints)
        
        val width = bitMatrix.width
        val height = bitMatrix.height
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.RGB_565)
        
        for (x in 0 until width) {
            for (y in 0 until height) {
                bitmap.setPixel(x, y, if (bitMatrix[x, y]) Color.BLACK else Color.WHITE)
            }
        }
        
        Timber.i("✓ QR Code generated: ${width}x${height}, format: RGB_565")
        return bitmap
    }
}
