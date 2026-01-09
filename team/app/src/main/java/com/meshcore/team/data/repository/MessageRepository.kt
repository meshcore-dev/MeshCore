package com.meshcore.team.data.repository

import com.meshcore.team.data.database.*
import kotlinx.coroutines.flow.Flow
import java.util.UUID

/**
 * Repository for Message operations
 * Mediates between database and UI layer
 */
class MessageRepository(
    private val messageDao: MessageDao,
    private val ackRecordDao: AckRecordDao
) {
    
    fun getMessagesByChannel(channelHash: Byte): Flow<List<MessageEntity>> {
        return messageDao.getMessagesByChannel(channelHash)
    }
    
    fun getAllMessages(): Flow<List<MessageEntity>> {
        return messageDao.getAllMessages()
    }
    
    suspend fun getMessageById(messageId: String): MessageEntity? {
        return messageDao.getMessageById(messageId)
    }
    
    suspend fun insertMessage(message: MessageEntity) {
        messageDao.insertMessage(message)
    }
    
    suspend fun updateMessage(message: MessageEntity) {
        messageDao.updateMessage(message)
    }
    
    suspend fun deleteMessage(message: MessageEntity) {
        messageDao.deleteMessage(message)
        // Also delete associated ACKs
        ackRecordDao.deleteAcksByMessageId(message.id)
    }
    
    suspend fun createOutgoingMessage(
        senderId: ByteArray,
        senderName: String?,
        channelHash: Byte,
        content: String,
        isPrivate: Boolean
    ): MessageEntity {
        val message = MessageEntity(
            id = UUID.randomUUID().toString(),
            senderId = senderId,
            senderName = senderName,
            channelHash = channelHash,
            content = content,
            timestamp = System.currentTimeMillis(),
            isPrivate = isPrivate,
            ackChecksum = null, // Will be set when device sends
            deliveryStatus = DeliveryStatus.SENDING.name,
            heardByCount = 0,
            attempt = 0,
            isSentByMe = true
        )
        insertMessage(message)
        return message
    }
    
    suspend fun updateDeliveryStatus(messageId: String, status: DeliveryStatus, count: Int) {
        messageDao.updateDeliveryStatus(messageId, status.name, count)
    }
    
    fun getAcksByMessageId(messageId: String): Flow<List<AckRecordEntity>> {
        return ackRecordDao.getAcksByMessageId(messageId)
    }
    
    suspend fun recordAck(
        messageId: String,
        ackChecksum: ByteArray,
        devicePublicKey: ByteArray,
        roundTripTimeMs: Int,
        isDirect: Boolean
    ) {
        val ack = AckRecordEntity(
            messageId = messageId,
            ackChecksum = ackChecksum,
            devicePublicKey = devicePublicKey,
            roundTripTimeMs = roundTripTimeMs,
            isDirect = isDirect,
            receivedAt = System.currentTimeMillis()
        )
        ackRecordDao.insertAck(ack)
        
        // Update message delivery count
        val count = ackRecordDao.getAckCountForMessage(messageId)
        updateDeliveryStatus(messageId, DeliveryStatus.DELIVERED, count)
    }
}
