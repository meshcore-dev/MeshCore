package com.meshcore.team.data.database

import androidx.room.Entity
import androidx.room.PrimaryKey

/**
 * Message entity for Room database
 * Stores sent and received messages with delivery tracking
 */
@Entity(tableName = "messages")
data class MessageEntity(
    @PrimaryKey val id: String,
    val senderId: ByteArray,
    val senderName: String?,
    val channelHash: Byte,
    val content: String,
    val timestamp: Long,
    val isPrivate: Boolean,
    val ackChecksum: ByteArray?,
    val deliveryStatus: String, // SENDING, SENT, DELIVERED
    val heardByCount: Int,
    val attempt: Int,
    val isSentByMe: Boolean
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as MessageEntity
        if (id != other.id) return false
        if (!senderId.contentEquals(other.senderId)) return false
        if (channelHash != other.channelHash) return false
        if (deliveryStatus != other.deliveryStatus) return false
        if (content != other.content) return false
        if (heardByCount != other.heardByCount) return false
        return true
    }

    override fun hashCode(): Int {
        var result = id.hashCode()
        result = 31 * result + senderId.contentHashCode()
        result = 31 * result + channelHash
        result = 31 * result + deliveryStatus.hashCode()
        result = 31 * result + content.hashCode()
        result = 31 * result + heardByCount
        return result
    }
}

/**
 * Delivery status enum
 */
enum class DeliveryStatus {
    SENDING,     // Message queued/transmitting
    SENT,        // Transmitted successfully
    DELIVERED    // At least one ACK received
}
