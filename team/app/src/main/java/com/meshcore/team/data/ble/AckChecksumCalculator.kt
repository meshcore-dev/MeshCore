package com.meshcore.team.data.ble

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

/**
 * Calculate ACK checksum for mesh messages
 * Checksum = first 4 bytes of SHA256(timestamp + attempt + text + sender_pubkey)
 */
object AckChecksumCalculator {
    
    /**
     * Calculate ACK checksum for a channel message
     * This matches the MeshCore firmware calculation:
     * SHA256(timestamp + txt_type_and_attempt + text + sender_pubkey)[0:4]
     * 
     * @param timestamp Unix timestamp (seconds) as 32-bit integer
     * @param attempt Message attempt number (0-3)
     * @param text Message text content
     * @param senderPubKey Sender's Ed25519 public key (32 bytes)
     * @return First 4 bytes of SHA256 hash
     */
    fun calculateChecksum(
        timestamp: Int,
        attempt: Int,
        text: String,
        senderPubKey: ByteArray
    ): ByteArray {
        require(senderPubKey.size == 32) { "Sender public key must be 32 bytes" }
        require(attempt in 0..3) { "Attempt must be 0-3" }
        
        // txt_type_and_attempt byte: upper 6 bits = txt_type (0 for plain), lower 2 bits = attempt
        val txtTypeAndAttempt: Byte = (0x00.shl(2) or (attempt and 0x03)).toByte()
        
        // Build the data to hash: timestamp (4) + txt_type_and_attempt (1) + text + pubkey (32)
        val textBytes = text.toByteArray(Charsets.UTF_8)
        val totalSize = 4 + 1 + textBytes.size + 32
        val dataToHash = ByteArray(totalSize)
        
        // Write timestamp (little-endian)
        ByteBuffer.wrap(dataToHash, 0, 4)
            .order(ByteOrder.LITTLE_ENDIAN)
            .putInt(timestamp)
        
        // Write txt_type_and_attempt
        dataToHash[4] = txtTypeAndAttempt
        
        // Write text
        textBytes.copyInto(dataToHash, 5)
        
        // Write sender public key
        senderPubKey.copyInto(dataToHash, 5 + textBytes.size)
        
        // Calculate SHA256 and return first 4 bytes
        val digest = MessageDigest.getInstance("SHA-256")
        val fullHash = digest.digest(dataToHash)
        
        return fullHash.copyOfRange(0, 4)
    }
}
