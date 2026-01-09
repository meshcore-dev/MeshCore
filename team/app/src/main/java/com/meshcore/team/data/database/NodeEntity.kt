package com.meshcore.team.data.database

import androidx.room.Entity
import androidx.room.PrimaryKey

/**
 * Node/Contact entity for Room database
 * Stores mesh network nodes with their information
 */
@Entity(tableName = "nodes")
data class NodeEntity(
    @PrimaryKey val publicKey: ByteArray,
    val hash: Byte,
    val name: String?,
    val latitude: Double?,
    val longitude: Double?,
    val lastSeen: Long,
    val batteryMilliVolts: Int?,
    val isRepeater: Boolean,
    val isRoomServer: Boolean,
    val isDirect: Boolean // True if last heard directly (no hops)
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as NodeEntity
        return publicKey.contentEquals(other.publicKey)
    }

    override fun hashCode(): Int {
        return publicKey.contentHashCode()
    }
}
