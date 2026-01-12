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
    val isDirect: Boolean, // True if last heard directly (no hops)
    val hopCount: Int // Number of relay hops to reach this node (0 = direct)
) {
    
    /**
     * Get connectivity status for map color coding
     * GREEN: Direct neighbor (0 hops, heard recently)
     * YELLOW: Relayed connection (1-3 hops)
     * RED: Not heard recently or many hops (4+)
     */
    fun getConnectivityStatus(staleThresholdMs: Long = 5 * 60 * 1000): ConnectivityStatus {
        val timeSinceLastSeen = System.currentTimeMillis() - lastSeen
        
        return when {
            timeSinceLastSeen > staleThresholdMs -> ConnectivityStatus.OFFLINE
            hopCount == 0 && isDirect -> ConnectivityStatus.DIRECT
            hopCount in 1..3 -> ConnectivityStatus.RELAYED
            else -> ConnectivityStatus.DISTANT
        }
    }
    
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

/**
 * Connectivity status for map visualization
 */
enum class ConnectivityStatus {
    DIRECT,    // Green: 0 hops, direct radio contact
    RELAYED,   // Yellow: 1-3 hops through repeaters
    DISTANT,   // Orange: 4+ hops, weak connection
    OFFLINE    // Red/Gray: Not heard recently
}
