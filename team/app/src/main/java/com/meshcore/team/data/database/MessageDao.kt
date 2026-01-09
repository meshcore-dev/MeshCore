package com.meshcore.team.data.database

import androidx.room.*
import kotlinx.coroutines.flow.Flow

/**
 * DAO for Message operations
 */
@Dao
interface MessageDao {
    
    @Query("SELECT * FROM messages WHERE channelHash = :channelHash ORDER BY timestamp DESC")
    fun getMessagesByChannel(channelHash: Byte): Flow<List<MessageEntity>>
    
    @Query("SELECT * FROM messages ORDER BY timestamp DESC")
    fun getAllMessages(): Flow<List<MessageEntity>>
    
    @Query("SELECT * FROM messages WHERE id = :messageId")
    suspend fun getMessageById(messageId: String): MessageEntity?
    
    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertMessage(message: MessageEntity)
    
    @Update
    suspend fun updateMessage(message: MessageEntity)
    
    @Delete
    suspend fun deleteMessage(message: MessageEntity)
    
    @Query("DELETE FROM messages WHERE channelHash = :channelHash")
    suspend fun deleteMessagesByChannel(channelHash: Byte)
    
    @Query("UPDATE messages SET deliveryStatus = :status, heardByCount = :count WHERE id = :messageId")
    suspend fun updateDeliveryStatus(messageId: String, status: String, count: Int)
}
