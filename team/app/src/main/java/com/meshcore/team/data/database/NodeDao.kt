package com.meshcore.team.data.database

import androidx.room.*
import kotlinx.coroutines.flow.Flow

/**
 * DAO for Node/Contact operations
 */
@Dao
interface NodeDao {
    
    @Query("SELECT * FROM nodes ORDER BY lastSeen DESC")
    fun getAllNodes(): Flow<List<NodeEntity>>
    
    @Query("SELECT * FROM nodes ORDER BY lastSeen DESC")
    suspend fun getAllNodesDirect(): List<NodeEntity>
    
    @Query("SELECT * FROM nodes WHERE publicKey = :publicKey")
    suspend fun getNodeByPublicKey(publicKey: ByteArray): NodeEntity?
    
    @Query("SELECT * FROM nodes WHERE hash = :hash")
    suspend fun getNodeByHash(hash: Byte): NodeEntity?
    
    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertNode(node: NodeEntity)
    
    @Update
    suspend fun updateNode(node: NodeEntity)
    
    @Delete
    suspend fun deleteNode(node: NodeEntity)
    
    @Query("DELETE FROM nodes")
    suspend fun deleteAllNodes()
    
    @Query("UPDATE nodes SET latitude = :latitude, longitude = :longitude, lastSeen = :lastSeen WHERE publicKey = :publicKey")
    suspend fun updateNodeLocation(publicKey: ByteArray, latitude: Double, longitude: Double, lastSeen: Long)
    
    @Query("UPDATE nodes SET lastSeen = :lastSeen, isDirect = :isDirect WHERE publicKey = :publicKey")
    suspend fun updateNodeLastSeen(publicKey: ByteArray, lastSeen: Long, isDirect: Boolean)
}
