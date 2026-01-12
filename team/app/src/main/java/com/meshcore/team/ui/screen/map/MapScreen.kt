package com.meshcore.team.ui.screen.map

import android.Manifest
import android.content.pm.PackageManager
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.MyLocation
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import org.osmdroid.config.Configuration
import org.osmdroid.tileprovider.tilesource.TileSourceFactory
import org.osmdroid.util.GeoPoint
import org.osmdroid.views.MapView
import org.osmdroid.views.overlay.Marker
import org.osmdroid.views.overlay.mylocation.GpsMyLocationProvider
import org.osmdroid.views.overlay.mylocation.MyLocationNewOverlay

/**
 * Map screen showing current location and mesh nodes
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MapScreen(
    viewModel: MapViewModel,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val currentLocation by viewModel.currentLocation.collectAsState()
    val nodes by viewModel.nodes.collectAsState()
    val locationSource by viewModel.locationSource.collectAsState()
    
    var mapView by remember { mutableStateOf<MapView?>(null) }
    var myLocationOverlay by remember { mutableStateOf<MyLocationNewOverlay?>(null) }
    var showLegend by remember { mutableStateOf(false) }
    
    // Initialize osmdroid configuration
    LaunchedEffect(Unit) {
        Configuration.getInstance().userAgentValue = context.packageName
    }
    
    // Update map center when location changes
    LaunchedEffect(currentLocation) {
        currentLocation?.let { location ->
            mapView?.controller?.apply {
                setCenter(GeoPoint(location.latitude, location.longitude))
                setZoom(15.0)
            }
        }
    }
    
    Scaffold(
        topBar = {
            TopAppBar(
                title = { 
                    Column {
                        Text("Map")
                        Text(
                            text = when (locationSource) {
                                "phone" -> "Using Phone GPS"
                                "companion" -> "Using Companion GPS"
                                else -> "No GPS"
                            },
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                },
                actions = {
                    IconButton(onClick = { showLegend = !showLegend }) {
                        Icon(Icons.Default.Info, "Network Status Legend")
                    }
                    DropdownMenu(
                        expanded = showLegend,
                        onDismissRequest = { showLegend = false }
                    ) {
                        Column(
                            modifier = Modifier
                                .padding(16.dp)
                                .width(220.dp)
                        ) {
                            Text(
                                "Network Status",
                                style = MaterialTheme.typography.titleSmall,
                                fontWeight = androidx.compose.ui.text.font.FontWeight.Bold
                            )
                            Spacer(modifier = Modifier.height(8.dp))
                            LegendItem(
                                color = com.meshcore.team.ui.theme.ConnectivityColors.Direct,
                                label = "Direct",
                                description = "0 hops"
                            )
                            LegendItem(
                                color = com.meshcore.team.ui.theme.ConnectivityColors.Relayed,
                                label = "Relayed",
                                description = "1-3 hops"
                            )
                            LegendItem(
                                color = com.meshcore.team.ui.theme.ConnectivityColors.Distant,
                                label = "Distant",
                                description = "4+ hops"
                            )
                            LegendItem(
                                color = com.meshcore.team.ui.theme.ConnectivityColors.Offline,
                                label = "Offline",
                                description = "Not heard recently"
                            )
                        }
                    }
                }
            )
        },
        floatingActionButton = {
            FloatingActionButton(
                onClick = {
                    currentLocation?.let { location ->
                        mapView?.controller?.animateTo(
                            GeoPoint(location.latitude, location.longitude)
                        )
                    }
                }
            ) {
                Icon(Icons.Default.MyLocation, "Center on location")
            }
        }
    ) { padding ->
        Box(
            modifier = modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            // osmdroid MapView
            AndroidView(
                factory = { ctx ->
                    MapView(ctx).apply {
                        mapView = this
                        setTileSource(TileSourceFactory.MAPNIK)
                        setMultiTouchControls(true)
                        
                        // Set initial position
                        controller.setZoom(15.0)
                        controller.setCenter(GeoPoint(0.0, 0.0))
                        
                        // Add location overlay if permission granted
                        if (ContextCompat.checkSelfPermission(
                                ctx, 
                                Manifest.permission.ACCESS_FINE_LOCATION
                            ) == PackageManager.PERMISSION_GRANTED
                        ) {
                            val locationOverlay = MyLocationNewOverlay(
                                GpsMyLocationProvider(ctx),
                                this
                            ).apply {
                                enableMyLocation()
                                enableFollowLocation()
                                // Don't draw the person icon, only the accuracy circle
                                setDrawAccuracyEnabled(true)
                            }
                            myLocationOverlay = locationOverlay
                            overlays.add(locationOverlay)
                        }
                    }
                },
                update = { view ->
                    // Update markers when nodes change
                    view.overlays.removeAll { it is Marker }
                    
                    timber.log.Timber.i("📍 Updating map with ${nodes.size} nodes")
                    
                    nodes.forEach { node ->
                        if (node.latitude != null && node.longitude != null) {
                            val status = node.getConnectivityStatus()
                            timber.log.Timber.i("📍 Map displaying node '${node.name}': lat=${node.latitude}, lon=${node.longitude}, hops=${node.hopCount}, status=$status")
                            
                            val marker = Marker(view).apply {
                                position = GeoPoint(node.latitude, node.longitude)
                                title = node.name ?: "Unknown"
                                timber.log.Timber.i("🗺️ Map marker created: '${node.name}' at ${node.latitude},${node.longitude}")
                                snippet = buildString {
                                    append("ID: ${node.hash.toInt() and 0xFF}")
                                    append("\nHops: ${node.hopCount}")
                                    append("\nStatus: $status")
                                }
                                
                                // Set marker icon based on connectivity (person icons with better colors)
                                icon = when (status) {
                                    com.meshcore.team.data.database.ConnectivityStatus.DIRECT -> {
                                        // Dark green person for direct connection
                                        view.context.getDrawable(org.osmdroid.library.R.drawable.person)?.apply {
                                            setTint(android.graphics.Color.rgb(34, 139, 34)) // Forest green
                                        }
                                    }
                                    com.meshcore.team.data.database.ConnectivityStatus.RELAYED -> {
                                        // Orange person for relayed connection
                                        view.context.getDrawable(org.osmdroid.library.R.drawable.person)?.apply {
                                            setTint(android.graphics.Color.rgb(255, 140, 0)) // Dark orange
                                        }
                                    }
                                    com.meshcore.team.data.database.ConnectivityStatus.DISTANT -> {
                                        // Red-orange person for distant connection (4+ hops)
                                        view.context.getDrawable(org.osmdroid.library.R.drawable.person)?.apply {
                                            setTint(android.graphics.Color.rgb(255, 69, 0)) // Red-orange
                                        }
                                    }
                                    com.meshcore.team.data.database.ConnectivityStatus.OFFLINE -> {
                                        // Dark red person for offline/stale
                                        view.context.getDrawable(org.osmdroid.library.R.drawable.person)?.apply {
                                            setTint(android.graphics.Color.rgb(139, 0, 0)) // Dark red
                                        }
                                    }
                                }
                                setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_BOTTOM)
                            }
                            view.overlays.add(marker)
                        }
                    }
                    
                    timber.log.Timber.i("📍 Map updated with ${view.overlays.count { it is Marker }} markers")
                    view.invalidate()
                },
                modifier = Modifier.fillMaxSize()
            )
        }
    }
    
    DisposableEffect(Unit) {
        onDispose {
            mapView?.onDetach()
        }
    }
}

data class LocationData(
    val latitude: Double,
    val longitude: Double,
    val altitude: Double? = null,
    val accuracy: Float? = null
)

@Composable
private fun LegendItem(color: androidx.compose.ui.graphics.Color, label: String, description: String) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        modifier = Modifier.padding(vertical = 4.dp)
    ) {
        Box(
            modifier = Modifier
                .size(16.dp)
                .background(color, androidx.compose.foundation.shape.CircleShape)
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
