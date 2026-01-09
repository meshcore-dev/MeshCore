package com.meshcore.team.data.ble

import timber.log.Timber
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Parses binary responses from MeshCore device
 */
object ResponseParser {
    
    /**
     * Parse response code from frame
     */
    fun getResponseCode(frame: ByteArray): Byte {
        return if (frame.isNotEmpty()) frame[0] else 0
    }
    
    /**
     * Parse RESP_CODE_SELF_INFO = 5
     * Device info response to CMD_APP_START
     * Contains: public key (32 bytes), node name, etc.
     */
    fun parseSelfInfo(frame: ByteArray): SelfInfo? {
        if (frame.isEmpty() || frame[0] != BleConstants.Responses.RESP_CODE_SELF_INFO) {
            Timber.e("Invalid SELF_INFO response")
            return null
        }
        
        if (frame.size < 33) { // At least response code + 32-byte pubkey
            Timber.e("SELF_INFO frame too short: ${frame.size} bytes")
            return null
        }
        
        val publicKey = frame.copyOfRange(1, 33)
        val name = if (frame.size > 33) {
            String(frame.copyOfRange(33, frame.size), Charsets.UTF_8)
        } else {
            ""
        }
        
        Timber.i("Parsed SELF_INFO: name=$name")
        return SelfInfo(publicKey, name)
    }
    
    /**
     * Parse PUSH_CODE_ADVERT = 0x80
     * Node advertisement with location
     * Format: [0x80][32-byte pubkey][4-byte lat][4-byte lon][name]
     */
    fun parseAdvertisement(frame: ByteArray): Advertisement? {
        if (frame.isEmpty() || frame[0] != BleConstants.PushCodes.PUSH_CODE_ADVERT) {
            return null
        }
        
        if (frame.size < 41) { // response code + 32 pubkey + 4 lat + 4 lon
            Timber.e("ADVERT frame too short: ${frame.size} bytes")
            return null
        }
        
        val publicKey = frame.copyOfRange(1, 33)
        
        val buffer = ByteBuffer.wrap(frame, 33, 8).order(ByteOrder.LITTLE_ENDIAN)
        val latInt = buffer.getInt()
        val lonInt = buffer.getInt()
        
        val latitude = latInt / 1_000_000.0
        val longitude = lonInt / 1_000_000.0
        
        val name = if (frame.size > 41) {
            String(frame.copyOfRange(41, frame.size), Charsets.UTF_8)
        } else {
            ""
        }
        
        Timber.d("Parsed ADVERT: name=$name, lat=$latitude, lon=$longitude")
        return Advertisement(publicKey, latitude, longitude, name)
    }
    
    /**
     * Parse PUSH_CODE_SEND_CONFIRMED = 0x82
     * Message delivery confirmation (ACK)
     * Format: [0x82][4-byte ACK checksum][4-byte RTT ms]
     */
    fun parseSendConfirmed(frame: ByteArray): SendConfirmed? {
        if (frame.isEmpty() || frame[0] != BleConstants.PushCodes.PUSH_CODE_SEND_CONFIRMED) {
            return null
        }
        
        if (frame.size < 9) {
            Timber.e("SEND_CONFIRMED frame too short: ${frame.size} bytes")
            return null
        }
        
        val ackChecksum = frame.copyOfRange(1, 5)
        
        val rtt = ByteBuffer.wrap(frame, 5, 4)
            .order(ByteOrder.LITTLE_ENDIAN)
            .getInt()
        
        Timber.d("Parsed SEND_CONFIRMED: RTT=${rtt}ms")
        return SendConfirmed(ackChecksum, rtt)
    }
    
    /**
     * Parse RESP_CODE_CHANNEL_MSG_RECV_V3 = 17
     * Channel message received
     */
    fun parseChannelMessage(frame: ByteArray): ChannelMessage? {
        if (frame.isEmpty() || frame[0] != BleConstants.Responses.RESP_CODE_CHANNEL_MSG_RECV_V3) {
            return null
        }
        
        if (frame.size < 42) { // response code + sender pubkey (32) + channel hash (1) + timestamp (4) + text (at least 1)
            Timber.e("CHANNEL_MSG frame too short: ${frame.size} bytes")
            return null
        }
        
        val senderPubKey = frame.copyOfRange(1, 33)
        val channelHash = frame[33]
        
        val timestamp = ByteBuffer.wrap(frame, 34, 4)
            .order(ByteOrder.LITTLE_ENDIAN)
            .getInt()
        
        val text = String(frame.copyOfRange(38, frame.size), Charsets.UTF_8)
        
        Timber.d("Parsed CHANNEL_MSG: channel=$channelHash, text=$text")
        return ChannelMessage(senderPubKey, channelHash, timestamp.toLong(), text)
    }
    
    /**
     * Check if response is OK
     */
    fun isOk(frame: ByteArray): Boolean {
        return frame.isNotEmpty() && frame[0] == BleConstants.Responses.RESP_CODE_OK
    }
    
    /**
     * Check if response is SENT confirmation
     */
    fun isSent(frame: ByteArray): Boolean {
        return frame.isNotEmpty() && frame[0] == BleConstants.Responses.RESP_CODE_SENT
    }
}

// Data classes for parsed responses

data class SelfInfo(
    val publicKey: ByteArray,
    val name: String
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as SelfInfo
        return publicKey.contentEquals(other.publicKey) && name == other.name
    }
    
    override fun hashCode(): Int {
        return 31 * publicKey.contentHashCode() + name.hashCode()
    }
}

data class Advertisement(
    val publicKey: ByteArray,
    val latitude: Double,
    val longitude: Double,
    val name: String
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as Advertisement
        return publicKey.contentEquals(other.publicKey) &&
                latitude == other.latitude &&
                longitude == other.longitude &&
                name == other.name
    }
    
    override fun hashCode(): Int {
        var result = publicKey.contentHashCode()
        result = 31 * result + latitude.hashCode()
        result = 31 * result + longitude.hashCode()
        result = 31 * result + name.hashCode()
        return result
    }
}

data class SendConfirmed(
    val ackChecksum: ByteArray,
    val roundTripTimeMs: Int
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as SendConfirmed
        return ackChecksum.contentEquals(other.ackChecksum) &&
                roundTripTimeMs == other.roundTripTimeMs
    }
    
    override fun hashCode(): Int {
        return 31 * ackChecksum.contentHashCode() + roundTripTimeMs
    }
}

data class ChannelMessage(
    val senderPublicKey: ByteArray,
    val channelHash: Byte,
    val timestamp: Long,
    val text: String
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as ChannelMessage
        return senderPublicKey.contentEquals(other.senderPublicKey) &&
                channelHash == other.channelHash &&
                timestamp == other.timestamp &&
                text == other.text
    }
    
    override fun hashCode(): Int {
        var result = senderPublicKey.contentHashCode()
        result = 31 * result + channelHash
        result = 31 * result + timestamp.hashCode()
        result = 31 * result + text.hashCode()
        return result
    }
}
