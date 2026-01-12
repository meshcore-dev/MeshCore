package com.meshcore.team.data.repository

import com.meshcore.team.data.ble.BleConnectionManager
import com.meshcore.team.data.ble.BleConstants
import com.meshcore.team.data.ble.CommandSerializer
import com.meshcore.team.data.database.NodeDao
import com.meshcore.team.data.database.NodeEntity
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import timber.log.Timber
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Repository for contact/node operations
 * Handles CMD_GET_CONTACTS and contact list management
 */
class ContactRepository(
    private val nodeDao: NodeDao,
    private val bleConnectionManager: BleConnectionManager
) {
    
    fun getAllContacts(): Flow<List<NodeEntity>> {
        return nodeDao.getAllNodes()
    }
    
    suspend fun getContactByPublicKey(publicKey: ByteArray): NodeEntity? {
        return nodeDao.getNodeByPublicKey(publicKey)
    }
    
    suspend fun getContactByHash(hash: Byte): NodeEntity? {
        return nodeDao.getNodeByHash(hash)
    }
    
    /**
     * Expose NodeDao for direct updates (e.g., from telemetry messages)
     */
    fun getNodeDao(): NodeDao {
        return nodeDao
    }
    
    /**
     * Request contact list from companion radio
     * Sends CMD_GET_CONTACTS and waits for responses
     */
    suspend fun syncContacts() {
        Timber.i("Requesting contact list from device...")
        val frame = CommandSerializer.getContacts()
        bleConnectionManager.sendData(frame)
    }
    
    /**
     * Parse RESP_CODE_CONTACT frame and store in database
     * Format: [code][32-byte pubkey][type][flags][path_len][8-byte path][32-byte name][4-timestamp][4-lat][4-lon][4-lastmod]
     */
    suspend fun parseContactResponse(frame: ByteArray) {
        try {
            if (frame.size < 89) {  // Minimum size
                Timber.e("Contact frame too small: ${frame.size} bytes")
                return
            }
            
            var offset = 1  // Skip response code
            
            // Extract public key (32 bytes)
            val publicKey = frame.copyOfRange(offset, offset + 32)
            offset += 32
            
            // Extract type, flags, path_len (skip path)
            val type = frame[offset++]
            val flags = frame[offset++]
            val pathLen = frame[offset++].toInt() and 0xFF
            offset += 8  // Skip 8-byte path
            
            // Extract name (32 bytes, null-terminated)
            val nameBytes = frame.copyOfRange(offset, offset + 32)
            val nullIndex = nameBytes.indexOf(0)
            val name = if (nullIndex >= 0) {
                String(nameBytes, 0, nullIndex, Charsets.UTF_8)
            } else {
                String(nameBytes, Charsets.UTF_8)
            }.trim()
            offset += 32
            
            // Extract timestamp (4 bytes)
            val timestamp = ByteBuffer.wrap(frame, offset, 4)
                .order(ByteOrder.LITTLE_ENDIAN)
                .getInt().toLong() and 0xFFFFFFFFL // Treat as unsigned
            offset += 4
            
            // Extract latitude (4 bytes)
            val latInt = ByteBuffer.wrap(frame, offset, 4)
                .order(ByteOrder.LITTLE_ENDIAN)
                .getInt()
            val latitude = if (latInt != 0) latInt / 1_000_000.0 else null
            offset += 4
            
            // Extract longitude (4 bytes)
            val lonInt = ByteBuffer.wrap(frame, offset, 4)
                .order(ByteOrder.LITTLE_ENDIAN)
                .getInt()
            val longitude = if (lonInt != 0) lonInt / 1_000_000.0 else null
            offset += 4
            
            // Extract lastmod (4 bytes)
            val lastmod = ByteBuffer.wrap(frame, offset, 4)
                .order(ByteOrder.LITTLE_ENDIAN)
                .getInt().toLong() and 0xFFFFFFFFL
            
            // Skip contacts with no name or invalid timestamp
            if (name.isEmpty()) {
                Timber.d("Skipping contact with empty name")
                return
            }
            
            // Use current time if timestamp is invalid (0 or very old)
            val currentTimeSecs = System.currentTimeMillis() / 1000
            val lastSeenMillis = if (timestamp > 0 && timestamp < currentTimeSecs) {
                timestamp * 1000  // Convert to milliseconds
            } else {
                System.currentTimeMillis()  // Use current time if invalid
            }
            
            // Create/update node entity
            val hash = publicKey[0]  // First byte as hash
            val isRepeater = (flags.toInt() and 0x01) != 0
            val isRoomServer = (flags.toInt() and 0x02) != 0
            val hopCount = pathLen.toInt() and 0xFF // Number of hops to reach this node
            
            val node = NodeEntity(
                publicKey = publicKey,
                hash = hash,
                name = name,
                latitude = latitude,
                longitude = longitude,
                lastSeen = lastSeenMillis,
                batteryMilliVolts = null,  // Not in contact frame
                isRepeater = isRepeater,
                isRoomServer = isRoomServer,
                isDirect = pathLen.toInt() == 0,  // Direct if no hops
                hopCount = hopCount
            )
            
            nodeDao.insertNode(node)
            Timber.i("✓ Contact synced: $name (hash=${hash.toString(16)}, hops=$hopCount)")
            
        } catch (e: Exception) {
            Timber.e(e, "Failed to parse contact response")
        }
    }
}
