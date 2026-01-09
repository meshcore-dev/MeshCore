package com.meshcore.team.data.database

import androidx.room.Entity
import androidx.room.PrimaryKey

/**
 * Channel entity for Room database
 * Stores public and private channels
 */
@Entity(tableName = "channels")
data class ChannelEntity(
    @PrimaryKey val hash: Byte,
    val name: String,
    val sharedKey: ByteArray,
    val isPublic: Boolean,
    val shareLocation: Boolean, // Location sharing enabled for this channel
    val createdAt: Long
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as ChannelEntity
        return hash == other.hash
    }

    override fun hashCode(): Int {
        return hash.toInt()
    }
}
