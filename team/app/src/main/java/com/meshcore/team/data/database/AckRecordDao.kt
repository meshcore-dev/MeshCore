package com.meshcore.team.data.database

import androidx.room.*
import kotlinx.coroutines.flow.Flow

/**
 * DAO for ACK Record operations
 */
@Dao
interface AckRecordDao {
    
    @Query("SELECT * FROM ack_records WHERE messageId = :messageId")
    fun getAcksByMessageId(messageId: String): Flow<List<AckRecordEntity>>
    
    @Query("SELECT * FROM ack_records WHERE ackChecksum = :checksum")
    suspend fun getAcksByChecksum(checksum: ByteArray): List<AckRecordEntity>
    
    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertAck(ack: AckRecordEntity)
    
    @Delete
    suspend fun deleteAck(ack: AckRecordEntity)
    
    @Query("DELETE FROM ack_records WHERE messageId = :messageId")
    suspend fun deleteAcksByMessageId(messageId: String)
    
    @Query("SELECT COUNT(*) FROM ack_records WHERE messageId = :messageId")
    suspend fun getAckCountForMessage(messageId: String): Int
    
    @Query("SELECT COUNT(*) FROM ack_records WHERE messageId = :messageId AND isDirect = 1")
    suspend fun getDirectAckCountForMessage(messageId: String): Int
}
