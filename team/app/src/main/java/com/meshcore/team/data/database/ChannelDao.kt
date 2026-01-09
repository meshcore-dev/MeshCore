package com.meshcore.team.data.database

import androidx.room.*
import kotlinx.coroutines.flow.Flow

/**
 * DAO for Channel operations
 */
@Dao
interface ChannelDao {
    
    @Query("SELECT * FROM channels ORDER BY createdAt DESC")
    fun getAllChannels(): Flow<List<ChannelEntity>>
    
    @Query("SELECT * FROM channels WHERE hash = :hash")
    suspend fun getChannelByHash(hash: Byte): ChannelEntity?
    
    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertChannel(channel: ChannelEntity)
    
    @Update
    suspend fun updateChannel(channel: ChannelEntity)
    
    @Delete
    suspend fun deleteChannel(channel: ChannelEntity)
    
    @Query("UPDATE channels SET shareLocation = :shareLocation WHERE hash = :hash")
    suspend fun updateChannelLocationSharing(hash: Byte, shareLocation: Boolean)
}
