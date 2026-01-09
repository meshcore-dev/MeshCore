package com.meshcore.team.data.repository

import com.meshcore.team.data.database.NodeDao
import com.meshcore.team.data.database.NodeEntity
import kotlinx.coroutines.flow.Flow

/**
 * Repository for Node/Contact operations
 * Mediates between database and UI layer
 */
class NodeRepository(private val nodeDao: NodeDao) {
    
    fun getAllNodes(): Flow<List<NodeEntity>> {
        return nodeDao.getAllNodes()
    }
    
    suspend fun getNodeByPublicKey(publicKey: ByteArray): NodeEntity? {
        return nodeDao.getNodeByPublicKey(publicKey)
    }
    
    suspend fun getNodeByHash(hash: Byte): NodeEntity? {
        return nodeDao.getNodeByHash(hash)
    }
    
    suspend fun insertNode(node: NodeEntity) {
        nodeDao.insertNode(node)
    }
    
    suspend fun updateNode(node: NodeEntity) {
        nodeDao.updateNode(node)
    }
    
    suspend fun upsertNodeFromAdvertisement(
        publicKey: ByteArray,
        hash: Byte,
        name: String?,
        latitude: Double?,
        longitude: Double?,
        isDirect: Boolean
    ) {
        val existing = nodeDao.getNodeByPublicKey(publicKey)
        
        if (existing != null) {
            // Update existing node
            val updated = existing.copy(
                name = name ?: existing.name,
                latitude = latitude,
                longitude = longitude,
                lastSeen = System.currentTimeMillis(),
                isDirect = isDirect
            )
            nodeDao.updateNode(updated)
        } else {
            // Insert new node
            val newNode = NodeEntity(
                publicKey = publicKey,
                hash = hash,
                name = name,
                latitude = latitude,
                longitude = longitude,
                lastSeen = System.currentTimeMillis(),
                batteryMilliVolts = null,
                isRepeater = false,
                isRoomServer = false,
                isDirect = isDirect
            )
            nodeDao.insertNode(newNode)
        }
    }
    
    suspend fun updateNodeLastSeen(publicKey: ByteArray, isDirect: Boolean) {
        nodeDao.updateNodeLastSeen(publicKey, System.currentTimeMillis(), isDirect)
    }
}
