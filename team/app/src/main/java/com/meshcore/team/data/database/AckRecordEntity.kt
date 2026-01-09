package com.meshcore.team.data.database

import androidx.room.Entity
import androidx.room.PrimaryKey

/**
 * ACK record entity for Room database
 * Tracks message delivery confirmations
 */
@Entity(tableName = "ack_records")
data class AckRecordEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val messageId: String,
    val ackChecksum: ByteArray,
    val devicePublicKey: ByteArray,
    val roundTripTimeMs: Int,
    val isDirect: Boolean, // True if direct contact (no hops)
    val receivedAt: Long
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as AckRecordEntity
        if (id != other.id) return false
        if (messageId != other.messageId) return false
        if (!ackChecksum.contentEquals(other.ackChecksum)) return false
        if (!devicePublicKey.contentEquals(other.devicePublicKey)) return false
        return true
    }

    override fun hashCode(): Int {
        var result = id.hashCode()
        result = 31 * result + messageId.hashCode()
        result = 31 * result + ackChecksum.contentHashCode()
        result = 31 * result + devicePublicKey.contentHashCode()
        return result
    }
}
