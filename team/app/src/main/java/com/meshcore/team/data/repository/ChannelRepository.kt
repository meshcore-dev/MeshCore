package com.meshcore.team.data.repository

import android.util.Base64
import com.meshcore.team.data.ble.BleConnectionManager
import com.meshcore.team.data.ble.BleConstants
import com.meshcore.team.data.ble.CommandSerializer
import com.meshcore.team.data.database.ChannelDao
import com.meshcore.team.data.database.ChannelEntity
import kotlinx.coroutines.flow.Flow
import timber.log.Timber
import java.security.MessageDigest

/**
 * Repository for Channel operations
 * Mediates between database and UI layer
 */
class ChannelRepository(
    private val channelDao: ChannelDao,
    private val bleConnectionManager: BleConnectionManager
) {
    
    fun getAllChannels(): Flow<List<ChannelEntity>> {
        return channelDao.getAllChannels()
    }
    
    suspend fun getAllChannelsOnce(): List<ChannelEntity> {
        return channelDao.getAllChannelsOnce()
    }
    
    suspend fun getChannelByHash(hash: Byte): ChannelEntity? {
        return channelDao.getChannelByHash(hash)
    }
    
    suspend fun getChannelByIndex(index: Byte): ChannelEntity? {
        return channelDao.getChannelByIndex(index)
    }
    
    suspend fun insertChannel(channel: ChannelEntity) {
        channelDao.insertChannel(channel)
    }
    
    suspend fun updateChannel(channel: ChannelEntity) {
        channelDao.updateChannel(channel)
    }
    
    suspend fun deleteChannel(channel: ChannelEntity) {
        channelDao.deleteChannel(channel)
    }
    
    suspend fun updateLocationSharing(hash: Byte, enabled: Boolean) {
        channelDao.updateChannelLocationSharing(hash, enabled)
    }
    
    /**
     * Initialize default public channel
     */
    suspend fun initializeDefaultChannel() {
        val publicKey = Base64.decode(BleConstants.PUBLIC_CHANNEL_PSK, Base64.DEFAULT)
        val hash = calculateChannelHash(publicKey)
        
        val existing = channelDao.getChannelByHash(hash)
        if (existing == null) {
            val publicChannel = ChannelEntity(
                hash = hash,
                name = "Public",
                sharedKey = publicKey,
                isPublic = true,
                shareLocation = false, // OFF by default for privacy
                channelIndex = 0, // Public channel is always index 0
                createdAt = System.currentTimeMillis()
            )
            channelDao.insertChannel(publicChannel)
        }
    }
    
    /**
     * Calculate channel hash from shared key
     * Hash is first byte of SHA256(key)
     */
    private fun calculateChannelHash(key: ByteArray): Byte {
        val digest = MessageDigest.getInstance("SHA-256")
        val hash = digest.digest(key)
        return hash[0]
    }
    
    /**
     * Create a new private channel with auto-generated PSK
     * @param name Channel display name
     * @return The created channel entity
     */
    suspend fun createPrivateChannel(name: String): ChannelEntity {
        // Generate random 16-byte PSK
        val psk = ByteArray(16)
        java.security.SecureRandom().nextBytes(psk)
        
        val hash = calculateChannelHash(psk)
        
        // Find next available channel index (1-3 for private channels)
        val existingChannels = channelDao.getAllChannelsOnce()
        val usedIndices = existingChannels.map { it.channelIndex.toInt() }.toSet()
        val nextIndex = (1..3).firstOrNull { it !in usedIndices } ?: 1 // Fallback to 1
        
        val channel = ChannelEntity(
            hash = hash,
            name = name,
            sharedKey = psk,
            isPublic = false,
            shareLocation = true, // Private channels require location sharing
            channelIndex = nextIndex.toByte(),
            createdAt = System.currentTimeMillis()
        )
        
        channelDao.insertChannel(channel)
        return channel
    }
    
    /**
     * Import a channel from meshcore:// URL or raw key
     * @param nameOrUrl Either channel name or full meshcore:// URL
     * @param keyData Either base64 PSK, hex PSK, or empty if URL provided
     * @return The imported channel entity, or null if invalid
     */
    suspend fun importChannel(nameOrUrl: String, keyData: String): ChannelEntity? {
        return try {
            var channelName: String
            var psk: ByteArray
            
            // Check if it's a meshcore:// URL
            if (nameOrUrl.startsWith("meshcore://channel/add?")) {
                val uri = android.net.Uri.parse(nameOrUrl)
                channelName = uri.getQueryParameter("name") ?: return null
                val secret = uri.getQueryParameter("secret") ?: return null
                psk = Base64.decode(secret, Base64.DEFAULT)
            } else {
                // Legacy format: separate name and key
                channelName = nameOrUrl
                val cleanKey = keyData.replace("\\s".toRegex(), "")
                
                psk = when {
                    // Base64 format (standard)
                    cleanKey.contains("+") || cleanKey.contains("/") || cleanKey.contains("=") -> {
                        Base64.decode(cleanKey, Base64.DEFAULT)
                    }
                    // Hex format (32 hex chars)
                    cleanKey.length == 32 && cleanKey.matches("[0-9a-fA-F]+".toRegex()) -> {
                        cleanKey.lowercase().chunked(2).map { it.toInt(16).toByte() }.toByteArray()
                    }
                    else -> return null
                }
            }
            
            if (psk.size != 16) return null
            
            val hash = calculateChannelHash(psk)
            
            // Check if channel already exists
            val existing = channelDao.getChannelByHash(hash)
            if (existing != null) return existing
            
            // Find next available channel index (1-3 for private channels)
            val existingChannels = channelDao.getAllChannelsOnce()
            val usedIndices = existingChannels.map { it.channelIndex.toInt() }.toSet()
            val nextIndex = (1..3).firstOrNull { it !in usedIndices } ?: 1
            
            val channel = ChannelEntity(
                hash = hash,
                name = channelName,
                sharedKey = psk,
                isPublic = false,
                shareLocation = true,
                channelIndex = nextIndex.toByte(),
                createdAt = System.currentTimeMillis()
            )
            
            channelDao.insertChannel(channel)
            channel
        } catch (e: Exception) {
            null
        }
    }
    
    /**
     * Export channel as meshcore:// URL for QR code sharing
     * Format: meshcore://channel/add?name=<name>&secret=<hex_secret>
     */
    fun exportChannelKey(channel: ChannelEntity): String {
        val hexSecret = channel.sharedKey.joinToString("") { "%02x".format(it) }
        val encodedName = java.net.URLEncoder.encode(channel.name, "UTF-8")
        return "meshcore://channel/add?name=$encodedName&secret=$hexSecret"
    }
    
    /**
     * Register channel with companion radio firmware
     * Must be called after creating/importing a channel for it to work
     */
    suspend fun registerChannelWithFirmware(channel: ChannelEntity): Boolean {
        return try {
            val command = CommandSerializer.setChannel(
                channelIndex = channel.channelIndex,
                name = channel.name,
                psk = channel.sharedKey
            )
            
            Timber.i("⚙️ Registering channel '${channel.name}' (idx=${channel.channelIndex}) with firmware")
            val success = bleConnectionManager.sendData(command)
            
            if (success) {
                Timber.i("✓ Channel '${channel.name}' registered with firmware")
            } else {
                Timber.e("✗ Failed to send SET_CHANNEL command for '${channel.name}'")
            }
            
            success
        } catch (e: Exception) {
            Timber.e(e, "Exception registering channel with firmware")
            false
        }
    }
    
    /**
     * Sync all channels with companion radio firmware
     * Call this when connecting to ensure firmware knows about all channels
     */
    suspend fun syncAllChannelsWithFirmware() {
        try {
            val channels = getAllChannelsOnce()
            Timber.i("🔄 Syncing ${channels.size} channels with companion radio firmware...")
            
            channels.forEach { channel ->
                registerChannelWithFirmware(channel)
                kotlinx.coroutines.delay(300) // Wait for BLE write to complete
            }
            
            Timber.i("✓ Channel sync complete")
        } catch (e: Exception) {
            Timber.e(e, "Failed to sync channels with firmware")
        }
    }
}
