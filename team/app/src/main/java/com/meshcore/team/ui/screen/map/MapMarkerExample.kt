package com.meshcore.team.ui.screen.map

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.meshcore.team.data.database.ConnectivityStatus
import com.meshcore.team.data.database.NodeEntity
import com.meshcore.team.ui.theme.ConnectivityColors

/**
 * Example composable showing how to use connectivity colors on a map
 * 
 * This is a reference implementation for when you build the actual map view.
 * You would replace this with Google Maps markers or similar.
 */
@Composable
fun MapMarkerExample(node: NodeEntity) {
    val connectivityStatus = node.getConnectivityStatus()
    val markerColor = ConnectivityColors.getColor(connectivityStatus)
    
    // Example: Simple colored dot with icon
    Box(
        modifier = Modifier.size(40.dp),
        contentAlignment = Alignment.Center
    ) {
        // Background circle with connectivity color
        Box(
            modifier = Modifier
                .size(36.dp)
                .background(markerColor, CircleShape)
        )
        
        // Icon overlay
        Icon(
            imageVector = Icons.Default.Person,
            contentDescription = node.name ?: "Contact",
            tint = Color.White,
            modifier = Modifier.size(20.dp)
        )
    }
}

/**
 * Legend showing what each color means
 */
@Composable
fun ConnectivityLegend(modifier: Modifier = Modifier) {
    Card(
        modifier = modifier.padding(16.dp),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.9f)
        )
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                "Network Status",
                style = MaterialTheme.typography.titleSmall,
                fontWeight = androidx.compose.ui.text.font.FontWeight.Bold
            )
            
            LegendItem(
                color = ConnectivityColors.Direct,
                label = "Direct",
                description = "0 hops"
            )
            LegendItem(
                color = ConnectivityColors.Relayed,
                label = "Relayed",
                description = "1-3 hops"
            )
            LegendItem(
                color = ConnectivityColors.Distant,
                label = "Distant",
                description = "4+ hops"
            )
            LegendItem(
                color = ConnectivityColors.Offline,
                label = "Offline",
                description = "Not heard recently"
            )
        }
    }
}

@Composable
private fun LegendItem(color: Color, label: String, description: String) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Box(
            modifier = Modifier
                .size(16.dp)
                .background(color, CircleShape)
        )
        Column {
            Text(
                label,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = androidx.compose.ui.text.font.FontWeight.SemiBold
            )
            Text(
                description,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

/**
 * Example usage with Google Maps (pseudo-code):
 * 
 * contacts.forEach { node ->
 *     if (node.latitude != null && node.longitude != null) {
 *         val position = LatLng(node.latitude, node.longitude)
 *         val color = ConnectivityColors.getColor(node.getConnectivityStatus())
 *         
 *         Marker(
 *             position = position,
 *             title = node.name,
 *             snippet = when(node.getConnectivityStatus()) {
 *                 ConnectivityStatus.DIRECT -> "Direct contact (${node.hopCount} hops)"
 *                 ConnectivityStatus.RELAYED -> "Relayed (${node.hopCount} hops)"
 *                 ConnectivityStatus.DISTANT -> "Distant (${node.hopCount} hops)"
 *                 ConnectivityStatus.OFFLINE -> "Offline"
 *             },
 *             icon = BitmapDescriptorFactory.defaultMarker(
 *                 when(color) {
 *                     ConnectivityColors.Direct -> BitmapDescriptorFactory.HUE_GREEN
 *                     ConnectivityColors.Relayed -> BitmapDescriptorFactory.HUE_YELLOW
 *                     ConnectivityColors.Distant -> BitmapDescriptorFactory.HUE_ORANGE
 *                     else -> BitmapDescriptorFactory.HUE_RED
 *                 }
 *             )
 *         )
 *     }
 * }
 */
