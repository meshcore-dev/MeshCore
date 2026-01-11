package com.meshcore.team.data.repository

import com.meshcore.team.data.database.*
import kotlinx.coroutines.flow.Flow
import timber.log.Timber
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
        val message = messageDao.getMessageById(messageId)
        Timber.d("updateDeliveryStatus: messageId=$messageId, found=${message != null}, oldStatus=${message?.deliveryStatus}")
        if (message != null) {
            val updated = message.copy(
                deliveryStatus = status.name,
                heardByCount = count
            )
            messageDao.updateMessage(updated)
            Timber.i("✓ Database updated: messageId=$messageId, status=${status.name}")
        } else {
            Timber.e("✗ Message not found in database: $messageId")
        }
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
        Timber.i("📥 recordAck called: messageId=$messageId, RTT=${roundTripTimeMs}ms")
        
        val ack = AckRecordEntity(
            messageId = messageId,
            ackChecksum = ackChecksum,
            devicePublicKey = devicePublicKey,
            roundTripTimeMs = roundTripTimeMs,
            isDirect = isDirect,
            receivedAt = System.currentTimeMillis()
        )
        ackRecordDao.insertAck(ack)
        Timber.d("✓ ACK inserted into database")
        
        // Update message delivery count
        val count = ackRecordDao.getAckCountForMessage(messageId)
        Timber.i("🔢 ACK count for message $messageId: $count")
        updateDeliveryStatus(messageId, DeliveryStatus.DELIVERED, count)
        Timber.i("✅ Message status updated to DELIVERED with count=$count")
    }
}
