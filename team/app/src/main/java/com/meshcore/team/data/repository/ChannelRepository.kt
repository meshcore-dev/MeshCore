package com.meshcore.team.data.repository

import android.util.Base64
import com.meshcore.team.data.ble.BleConstants
import com.meshcore.team.data.database.ChannelDao
import com.meshcore.team.data.database.ChannelEntity
import kotlinx.coroutines.flow.Flow
import java.security.MessageDigest

/**
 * Repository for Channel operations
 * Mediates between database and UI layer
 */
class ChannelRepository(private val channelDao: ChannelDao) {
    
    fun getAllChannels(): Flow<List<ChannelEntity>> {
        return channelDao.getAllChannels()
    }
    
    suspend fun getChannelByHash(hash: Byte): ChannelEntity? {
        return channelDao.getChannelByHash(hash)
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
}
