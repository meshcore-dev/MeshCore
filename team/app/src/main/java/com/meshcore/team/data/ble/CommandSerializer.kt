package com.meshcore.team.data.ble

import timber.log.Timber
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Serializes commands to binary frames for sending to MeshCore device
 * Max frame size: 172 bytes
 * Min write interval: 60ms
 */
object CommandSerializer {
    
    /**
     * CMD_APP_START = 1
     * Initialize connection with device
     * Format: [cmd][7 reserved bytes][app_name string]
     */
    fun appStart(): ByteArray {
        val appName = "TEAM"
        val frame = ByteArray(1 + 7 + appName.length)
        var offset = 0
        
        frame[offset++] = BleConstants.Commands.CMD_APP_START
        // 7 reserved bytes (leave as 0x00)
        offset += 7
        // App name
        appName.toByteArray(Charsets.UTF_8).copyInto(frame, offset)
        
        return frame
    }
    
    /**
     * CMD_SEND_TXT_MSG = 2
     * Send private text message to a contact
     * Format: [cmd][32-byte pubkey][4-byte timestamp][text]
     */
    fun sendTextMessage(publicKey: ByteArray, timestamp: Int, text: String): ByteArray {
        require(publicKey.size == 32) { "Public key must be 32 bytes" }
        
        val textBytes = text.toByteArray(Charsets.UTF_8)
        val frame = ByteArray(1 + 32 + 4 + textBytes.size)
        
        var offset = 0
        frame[offset++] = BleConstants.Commands.CMD_SEND_TXT_MSG
        
        // Copy public key
        publicKey.copyInto(frame, offset)
        offset += 32
        
        // Add timestamp (little-endian)
        ByteBuffer.wrap(frame, offset, 4)
            .order(ByteOrder.LITTLE_ENDIAN)
            .putInt(timestamp)
        offset += 4
        
        // Add text
        textBytes.copyInto(frame, offset)
        
        Timber.d("Serialized CMD_SEND_TXT_MSG: ${frame.size} bytes")
        return frame
    }
    
    /**
     * CMD_SEND_CHANNEL_TXT_MSG = 3
     * Send channel/group text message
     * Format: [cmd][txt_type][channel_idx][4-byte timestamp][text]
     */
    fun sendChannelTextMessage(channelHash: Byte, timestamp: Int, text: String): ByteArray {
        val textBytes = text.toByteArray(Charsets.UTF_8)
        val frame = ByteArray(1 + 1 + 1 + 4 + textBytes.size)
        
        var offset = 0
        frame[offset++] = BleConstants.Commands.CMD_SEND_CHANNEL_TXT_MSG
        frame[offset++] = 0  // TXT_TYPE_PLAIN = 0
        frame[offset++] = 0  // channel_idx = 0 (first/public channel)
        
        // Add timestamp (little-endian)
        ByteBuffer.wrap(frame, offset, 4)
            .order(ByteOrder.LITTLE_ENDIAN)
            .putInt(timestamp)
        offset += 4
        
        // Add text
        textBytes.copyInto(frame, offset)
        
        Timber.d("Serialized CMD_SEND_CHANNEL_TXT_MSG: ${frame.size} bytes")
        return frame
    }
    
    /**
     * CMD_GET_CONTACTS = 4
     * Request contact list from device
     */
    fun getContacts(): ByteArray {
        return byteArrayOf(BleConstants.Commands.CMD_GET_CONTACTS)
    }
    
    /**
     * CMD_SEND_SELF_ADVERT = 7
     * Broadcast node advertisement (location, name, etc.)
     */
    fun sendSelfAdvert(): ByteArray {
        return byteArrayOf(BleConstants.Commands.CMD_SEND_SELF_ADVERT)
    }
    
    /**
     * CMD_SYNC_NEXT_MESSAGE = 10
     * Poll for next incoming message
     */
    fun syncNextMessage(): ByteArray {
        return byteArrayOf(BleConstants.Commands.CMD_SYNC_NEXT_MESSAGE)
    }
    
    /**
     * CMD_SET_ADVERT_LATLON = 14
     * Set location for advertisements
     * Format: [cmd][4-byte lat * 1000000][4-byte lon * 1000000]
     */
    fun setAdvertLatLon(latitude: Double, longitude: Double): ByteArray {
        val frame = ByteArray(1 + 4 + 4)
        
        val latInt = (latitude * 1_000_000).toInt()
        val lonInt = (longitude * 1_000_000).toInt()
        
        var offset = 0
        frame[offset++] = BleConstants.Commands.CMD_SET_ADVERT_LATLON
        
        val buffer = ByteBuffer.wrap(frame, offset, 8).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(latInt)
        buffer.putInt(lonInt)
        
        Timber.d("Serialized CMD_SET_ADVERT_LATLON: lat=$latitude, lon=$longitude")
        return frame
    }
    
    /**
     * CMD_GET_CHANNEL = 31
     * Get channel information
     * Format: [cmd][1-byte channel hash]
     */
    fun getChannel(channelHash: Byte): ByteArray {
        return byteArrayOf(BleConstants.Commands.CMD_GET_CHANNEL, channelHash)
    }
    
    /**
     * CMD_SET_CHANNEL = 32
     * Create/update channel
     * Format: [cmd][1-byte hash][16-byte key][name string]
     */
    fun setChannel(hash: Byte, key: ByteArray, name: String): ByteArray {
        require(key.size == 16) { "Channel key must be 16 bytes" }
        
        val nameBytes = name.toByteArray(Charsets.UTF_8)
        val frame = ByteArray(1 + 1 + 16 + nameBytes.size)
        
        var offset = 0
        frame[offset++] = BleConstants.Commands.CMD_SET_CHANNEL
        frame[offset++] = hash
        
        // Copy key
        key.copyInto(frame, offset)
        offset += 16
        
        // Add name
        nameBytes.copyInto(frame, offset)
        
        Timber.d("Serialized CMD_SET_CHANNEL: ${frame.size} bytes")
        return frame
    }
}
