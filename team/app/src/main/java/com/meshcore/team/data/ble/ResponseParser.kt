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
     * Format: [resp_code][adv_type][tx_power][max_power][pub_key 32 bytes][lat 4][lon 4][multi_acks][advert_policy][telemetry_mode][manual_contacts][freq 4][bw 4][sf][cr][node_name]
     * Contains: public key (32 bytes), device settings, and node name
     */
    fun parseSelfInfo(frame: ByteArray): SelfInfo? {
        if (frame.isEmpty() || frame[0] != BleConstants.Responses.RESP_CODE_SELF_INFO) {
            Timber.e("Invalid SELF_INFO response")
            return null
        }
        
        // Minimum frame size: resp_code(1) + adv_type(1) + tx_power(1) + max_power(1) + pub_key(32) + lat(4) + lon(4) + flags(4) + freq(4) + bw(4) + sf(1) + cr(1) = 58 bytes
        if (frame.size < 58) {
            Timber.e("SELF_INFO frame too short: ${frame.size} bytes, expected at least 58")
            return null
        }
        
        // Extract public key (bytes 4-35, after resp_code + adv_type + tx_power + max_power)
        val publicKey = frame.copyOfRange(4, 36)
        
        // Node name starts at byte 58 (0-indexed) and goes to end of frame
        val name = if (frame.size > 58) {
            // Extract name bytes and convert to string, removing any null terminators
            val nameBytes = frame.copyOfRange(58, frame.size)
            // Find first null terminator if present
            val nullIndex = nameBytes.indexOf(0.toByte())
            val actualNameBytes = if (nullIndex != -1) nameBytes.copyOfRange(0, nullIndex) else nameBytes
            String(actualNameBytes, Charsets.UTF_8).trim()
        } else {
            ""
        }
        
        Timber.i("Device info: name='$name'")
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
     * Channel message received from CMD_SYNC_NEXT_MESSAGE
     * Format: [resp_code][snr][reserved1][reserved2][channel_idx][path_len][txt_type][timestamp 4 bytes][text]
     */
    fun parseChannelMessage(frame: ByteArray): ChannelMessage? {
        if (frame.isEmpty() || frame[0] != BleConstants.Responses.RESP_CODE_CHANNEL_MSG_RECV_V3) {
            return null
        }
        
        if (frame.size < 11) { // Minimum: resp + snr + res + res + ch_idx + path + type + timestamp
            Timber.e("CHANNEL_MSG_V3 frame too short: ${frame.size} bytes")
            return null
        }
        
        val snr = frame[1]
        // skip reserved bytes 2-3
        val channelIdx = frame[4]
        val pathLen = frame[5]
        val txtType = frame[6]
        
        val timestamp = ByteBuffer.wrap(frame, 7, 4)
            .order(ByteOrder.LITTLE_ENDIAN)
            .getInt()
            .toLong() and 0xFFFFFFFFL // unsigned to long
        
        val text = if (frame.size > 11) {
            String(frame.copyOfRange(11, frame.size), Charsets.UTF_8)
        } else {
            ""
        }
        
        Timber.i("Parsed CHANNEL_MSG: idx=$channelIdx, text='$text', timestamp=$timestamp, snr=$snr")
        return ChannelMessage(
            channelIdx = channelIdx,
            channelHash = channelIdx.toByte(), // For now, use idx as hash (need proper mapping later)
            timestamp = timestamp,
            text = text,
            snr = snr.toInt(),
            pathLen = pathLen.toInt() and 0xFF,
            senderPubKey = null // V3 format doesn't include sender pubkey in this message
        )
    }
    
    /**
     * Parse RESP_CODE_CONTACT_MSG_RECV_V3 = 16
     * Direct/private message received from CMD_SYNC_NEXT_MESSAGE
     * Format: [resp_code][snr][reserved1][reserved2][32-byte sender_pubkey][path_len][txt_type][timestamp 4 bytes][text]
     */
    fun parseDirectMessage(frame: ByteArray): DirectMessage? {
        if (frame.isEmpty() || frame[0] != BleConstants.Responses.RESP_CODE_CONTACT_MSG_RECV_V3) {
            return null
        }
        
        if (frame.size < 43) { // Minimum: resp + snr + 2 reserved + 32 pubkey + path + type + 4 timestamp
            Timber.e("CONTACT_MSG_V3 frame too short: ${frame.size} bytes")
            return null
        }
        
        val snr = frame[1]
        // skip reserved bytes 2-3
        val senderPubKey = frame.copyOfRange(4, 36)
        val pathLen = frame[36]
        val txtType = frame[37]
        
        val timestamp = ByteBuffer.wrap(frame, 38, 4)
            .order(ByteOrder.LITTLE_ENDIAN)
            .getInt()
            .toLong() and 0xFFFFFFFFL
        
        val text = if (frame.size > 42) {
            String(frame.copyOfRange(42, frame.size), Charsets.UTF_8)
        } else {
            ""
        }
        
        Timber.i("Parsed DIRECT_MSG: text='$text', timestamp=$timestamp, snr=$snr")
        return DirectMessage(
            senderPubKey = senderPubKey,
            timestamp = timestamp,
            text = text,
            snr = snr.toInt(),
            pathLen = pathLen.toInt() and 0xFF
        )
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
    val channelIdx: Byte,
    val channelHash: Byte,
    val timestamp: Long,
    val text: String,
    val snr: Int,
    val pathLen: Int,
    val senderPubKey: ByteArray?
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as ChannelMessage
        return channelIdx == other.channelIdx &&
                channelHash == other.channelHash &&
                timestamp == other.timestamp &&
                text == other.text &&
                snr == other.snr &&
                pathLen == other.pathLen &&
                senderPubKey?.contentEquals(other.senderPubKey) ?: (other.senderPubKey == null)
    }
    
    override fun hashCode(): Int {
        var result = channelIdx.toInt()
        result = 31 * result + channelHash.toInt()
        result = 31 * result + timestamp.hashCode()
        result = 31 * result + text.hashCode()
        result = 31 * result + snr
        result = 31 * result + pathLen
        result = 31 * result + (senderPubKey?.contentHashCode() ?: 0)
        return result
    }
}

data class DirectMessage(
    val senderPubKey: ByteArray,
    val timestamp: Long,
    val text: String,
    val snr: Int,
    val pathLen: Int
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as DirectMessage
        return senderPubKey.contentEquals(other.senderPubKey) &&
                timestamp == other.timestamp &&
                text == other.text &&
                snr == other.snr &&
                pathLen == other.pathLen
    }
    
    override fun hashCode(): Int {
        var result = senderPubKey.contentHashCode()
        result = 31 * result + timestamp.hashCode()
        result = 31 * result + text.hashCode()
        result = 31 * result + snr
        result = 31 * result + pathLen
        return result
    }
}
