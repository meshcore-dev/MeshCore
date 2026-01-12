package com.meshcore.team.ui.theme

import androidx.compose.ui.graphics.Color
import com.meshcore.team.data.database.ConnectivityStatus

/**
 * Color palette for map marker connectivity status
 */
object ConnectivityColors {
    
    // Green: Direct neighbor (0 hops)
    val Direct = Color(0xFF4CAF50)
    
    // Yellow: Relayed through repeaters (1-3 hops)
    val Relayed = Color(0xFFFFC107)
    
    // Orange: Distant connection (4+ hops)
    val Distant = Color(0xFFFF9800)
    
    // Red/Gray: Not heard recently (offline)
    val Offline = Color(0xFF9E9E9E)
    
    /**
     * Get color for a connectivity status
     */
    fun getColor(status: ConnectivityStatus): Color {
        return when (status) {
            ConnectivityStatus.DIRECT -> Direct
            ConnectivityStatus.RELAYED -> Relayed
            ConnectivityStatus.DISTANT -> Distant
            ConnectivityStatus.OFFLINE -> Offline
        }
    }
    
    /**
     * Get color for a node entity directly
     */
    fun getColorForNode(
        hopCount: Int,
        isDirect: Boolean,
        lastSeen: Long,
        staleThresholdMs: Long = 5 * 60 * 1000 // 5 minutes default
    ): Color {
        val timeSinceLastSeen = System.currentTimeMillis() - lastSeen
        
        return when {
            timeSinceLastSeen > staleThresholdMs -> Offline
            hopCount == 0 && isDirect -> Direct
            hopCount in 1..3 -> Relayed
            else -> Distant
        }
    }
}
