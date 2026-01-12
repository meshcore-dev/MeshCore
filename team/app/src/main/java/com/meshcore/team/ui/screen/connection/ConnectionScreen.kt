package com.meshcore.team.ui.screen.connection

import android.annotation.SuppressLint
import android.bluetooth.le.ScanResult
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.meshcore.team.data.ble.ConnectionState

/**
 * Connection screen for scanning and connecting to MeshCore devices
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConnectionScreen(viewModel: ConnectionViewModel) {
    val scannedDevices by viewModel.scannedDevices.collectAsState()
    val isScanning by viewModel.isScanning.collectAsState()
    val connectionState by viewModel.connectionState.collectAsState()
    val connectedDevice by viewModel.connectedDevice.collectAsState()
    val locationSource by viewModel.locationSource.collectAsState()
    val telemetryEnabled by viewModel.telemetryEnabled.collectAsState()
    val telemetryChannelHash by viewModel.telemetryChannelHash.collectAsState()
    val channels by viewModel.channels.collectAsState()
    val userAlias by viewModel.userAlias.collectAsState()
    
    var showDeviceSettings by remember { mutableStateOf(false) }
    var showLocationSettings by remember { mutableStateOf(false) }
    var showTelemetrySettings by remember { mutableStateOf(false) }
    
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Connect to Device") }
            )
        },
        floatingActionButton = {
            if (connectionState == ConnectionState.DISCONNECTED) {
                FloatingActionButton(
                    onClick = {
                        if (isScanning) {
                            viewModel.stopScan()
                        } else {
                            viewModel.startScan()
                        }
                    }
                ) {
                    Icon(
                        imageVector = if (isScanning) Icons.Default.Stop else Icons.Default.Refresh,
                        contentDescription = if (isScanning) "Stop Scan" else "Start Scan"
                    )
                }
            }
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            // Connection status card
            ConnectionStatusCard(
                connectionState = connectionState,
                deviceName = connectedDevice?.name ?: "Unknown",
                deviceAddress = connectedDevice?.address ?: "",
                onDisconnect = { viewModel.disconnect() }
            )
            
            // Companion settings section (when connected)
            if (connectionState == ConnectionState.CONNECTED) {
                Divider(modifier = Modifier.padding(vertical = 8.dp))
                
                CompanionSettingsSection(
                    deviceName = userAlias ?: "Not set",
                    locationSource = locationSource,
                    telemetryEnabled = telemetryEnabled,
                    telemetryChannel = channels.find { it.hash.toString() == telemetryChannelHash }?.name,
                    onDeviceNameClick = { showDeviceSettings = true },
                    onLocationSourceClick = { showLocationSettings = true },
                    onTelemetryClick = { showTelemetrySettings = true }
                )
            }
            
            Divider(modifier = Modifier.padding(vertical = 8.dp))
            
            // Device list
            if (connectionState == ConnectionState.DISCONNECTED) {
                if (isScanning) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        horizontalArrangement = Arrangement.Center,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        CircularProgressIndicator(modifier = Modifier.size(24.dp))
                        Spacer(modifier = Modifier.width(16.dp))
                        Text("Scanning for MeshCore devices...")
                    }
                }
                
                if (scannedDevices.isEmpty() && !isScanning) {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(16.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = "No devices found.\nTap the scan button to start.",
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                } else {
                    LazyColumn(
                        modifier = Modifier.fillMaxSize()
                    ) {
                        items(scannedDevices) { scanResult ->
                            DeviceListItem(
                                scanResult = scanResult,
                                onClick = { viewModel.connect(scanResult) }
                            )
                        }
                    }
                }
            }
        }
        
        // User alias settings dialog
        if (showDeviceSettings) {
            DeviceSettingsDialog(
                currentName = userAlias ?: "",
                onNameChange = { newAlias ->
                    viewModel.setUserAlias(newAlias)
                    showDeviceSettings = false
                },
                onDismiss = { showDeviceSettings = false }
            )
        }
        
        // Location source settings dialog
        if (showLocationSettings) {
            LocationSourceDialog(
                currentSource = locationSource,
                onSourceChange = { source ->
                    viewModel.setLocationSource(source)
                    showLocationSettings = false
                },
                onDismiss = { showLocationSettings = false }
            )
        }
        
        // Telemetry settings dialog
        if (showTelemetrySettings) {
            TelemetrySettingsDialog(
                enabled = telemetryEnabled,
                selectedChannelHash = telemetryChannelHash,
                channels = channels.filter { !it.isPublic }, // Only private channels
                onEnabledChange = { enabled ->
                    viewModel.setTelemetryEnabled(enabled)
                },
                onChannelChange = { channelHash ->
                    viewModel.setTelemetryChannel(channelHash)
                },
                onDismiss = { showTelemetrySettings = false }
            )
        }
    }
}

@Composable
fun CompanionSettingsSection(
    deviceName: String,
    locationSource: String,
    telemetryEnabled: Boolean,
    telemetryChannel: String?,
    onDeviceNameClick: () -> Unit,
    onLocationSourceClick: () -> Unit,
    onTelemetryClick: () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp)
    ) {
        Text(
            text = "Companion Settings",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.padding(bottom = 8.dp)
        )
        
        // User alias setting
        Card(
            modifier = Modifier
                .fillMaxWidth()
                .clickable(onClick = onDeviceNameClick),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceVariant
            )
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "User Alias",
                        style = MaterialTheme.typography.bodyMedium,
                        fontWeight = FontWeight.Medium
                    )
                    Text(
                        text = deviceName.ifBlank { "Not set" },
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Icon(
                    imageVector = Icons.Default.Edit,
                    contentDescription = "Edit user alias",
                    tint = MaterialTheme.colorScheme.primary
                )
            }
        }
        
        Spacer(modifier = Modifier.height(8.dp))
        
        // Location source setting
        Card(
            modifier = Modifier
                .fillMaxWidth()
                .clickable(onClick = onLocationSourceClick),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceVariant
            )
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "Location Source",
                        style = MaterialTheme.typography.bodyMedium,
                        fontWeight = FontWeight.Medium
                    )
                    Text(
                        text = when (locationSource) {
                            "phone" -> "Phone GPS"
                            "companion" -> "Companion Radio GPS"
                            else -> "Not set"
                        },
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Icon(
                    imageVector = Icons.Default.LocationOn,
                    contentDescription = "Change location source",
                    tint = MaterialTheme.colorScheme.primary
                )
            }
        }
        
        Spacer(modifier = Modifier.height(8.dp))
        
        // Telemetry tracking setting
        Card(
            modifier = Modifier
                .fillMaxWidth()
                .clickable(onClick = onTelemetryClick),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceVariant
            )
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "Location Tracking",
                        style = MaterialTheme.typography.bodyMedium,
                        fontWeight = FontWeight.Medium
                    )
                    Text(
                        text = if (telemetryEnabled) {
                            telemetryChannel?.let { "Enabled on \"$it\"" } ?: "Enabled (no channel)"
                        } else {
                            "Disabled"
                        },
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Icon(
                    imageVector = if (telemetryEnabled) Icons.Default.CheckCircle else Icons.Default.LocationOff,
                    contentDescription = "Configure telemetry",
                    tint = if (telemetryEnabled) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
fun ConnectionStatusCard(
    connectionState: ConnectionState,
    deviceName: String,
    deviceAddress: String,
    onDisconnect: () -> Unit
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(16.dp),
        colors = CardDefaults.cardColors(
            containerColor = when (connectionState) {
                ConnectionState.CONNECTED -> MaterialTheme.colorScheme.primaryContainer
                ConnectionState.CONNECTING -> MaterialTheme.colorScheme.tertiaryContainer
                ConnectionState.ERROR -> MaterialTheme.colorScheme.errorContainer
                else -> MaterialTheme.colorScheme.surfaceVariant
            }
        )
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = when (connectionState) {
                        ConnectionState.DISCONNECTED -> "Disconnected"
                        ConnectionState.CONNECTING -> "Connecting..."
                        ConnectionState.CONNECTED -> "Connected"
                        ConnectionState.DISCONNECTING -> "Disconnecting..."
                        ConnectionState.ERROR -> "Error"
                    },
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold
                )
                
                if (connectionState == ConnectionState.CONNECTED) {
                    Text(
                        text = deviceName,
                        style = MaterialTheme.typography.bodyMedium
                    )
                    Text(
                        text = deviceAddress,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            
            when (connectionState) {
                ConnectionState.CONNECTED -> {
                    IconButton(onClick = onDisconnect) {
                        Icon(
                            imageVector = Icons.Default.Close,
                            contentDescription = "Disconnect",
                            tint = MaterialTheme.colorScheme.error
                        )
                    }
                }
                ConnectionState.CONNECTING -> {
                    CircularProgressIndicator(modifier = Modifier.size(24.dp))
                }
                else -> {}
            }
        }
    }
}

@SuppressLint("MissingPermission")
@Composable
fun DeviceListItem(
    scanResult: ScanResult,
    onClick: () -> Unit
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 4.dp)
            .clickable(onClick = onClick)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = scanResult.device.name ?: "Unknown Device",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = scanResult.device.address,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Text(
                    text = "RSSI: ${scanResult.rssi} dBm",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            
            Icon(
                imageVector = Icons.Default.ChevronRight,
                contentDescription = "Connect",
                tint = MaterialTheme.colorScheme.primary
            )
        }
    }
}

@Composable
fun DeviceSettingsDialog(
    currentName: String,
    onNameChange: (String) -> Unit,
    onDismiss: () -> Unit
) {
    var deviceName by remember { mutableStateOf(currentName) }
    
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("User Alias") },
        text = {
            Column {
                Text(
                    text = "Set your display name for messages and telemetry. This is only used in this app and doesn't change your device settings.",
                    style = MaterialTheme.typography.bodyMedium
                )
                Spacer(modifier = Modifier.height(16.dp))
                OutlinedTextField(
                    value = deviceName,
                    onValueChange = { deviceName = it },
                    label = { Text("User Alias") },
                    placeholder = { Text("e.g., John's Radio") },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true
                )
            }
        },
        confirmButton = {
            TextButton(
                onClick = { onNameChange(deviceName.trim()) },
                enabled = deviceName.trim().isNotBlank()
            ) {
                Text("Save")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancel")
            }
        }
    )
}

@Composable
fun LocationSourceDialog(
    currentSource: String,
    onSourceChange: (String) -> Unit,
    onDismiss: () -> Unit
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Location Source") },
        text = {
            Column {
                Text(
                    text = "Select which GPS source to use for your location:",
                    style = MaterialTheme.typography.bodyMedium
                )
                Spacer(modifier = Modifier.height(16.dp))
                
                // Phone GPS option
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { onSourceChange("phone") },
                    colors = CardDefaults.cardColors(
                        containerColor = if (currentSource == "phone")
                            MaterialTheme.colorScheme.primaryContainer
                        else
                            MaterialTheme.colorScheme.surface
                    )
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = "Phone GPS",
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.Bold
                            )
                            Text(
                                text = "Use this phone's GPS receiver",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        if (currentSource == "phone") {
                            Icon(
                                imageVector = Icons.Default.Check,
                                contentDescription = "Selected",
                                tint = MaterialTheme.colorScheme.primary
                            )
                        }
                    }
                }
                
                Spacer(modifier = Modifier.height(8.dp))
                
                // Companion Radio GPS option
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { onSourceChange("companion") },
                    colors = CardDefaults.cardColors(
                        containerColor = if (currentSource == "companion")
                            MaterialTheme.colorScheme.primaryContainer
                        else
                            MaterialTheme.colorScheme.surface
                    )
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = "Companion Radio GPS",
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.Bold
                            )
                            Text(
                                text = "Use companion radio's GPS (if available)",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        if (currentSource == "companion") {
                            Icon(
                                imageVector = Icons.Default.Check,
                                contentDescription = "Selected",
                                tint = MaterialTheme.colorScheme.primary
                            )
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text("Close")
            }
        }
    )
}

@Composable
fun TelemetrySettingsDialog(
    enabled: Boolean,
    selectedChannelHash: String?,
    channels: List<com.meshcore.team.data.database.ChannelEntity>,
    onEnabledChange: (Boolean) -> Unit,
    onChannelChange: (String?) -> Unit,
    onDismiss: () -> Unit
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Location Tracking Settings") },
        text = {
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Text(
                    text = "Periodically share your location with other mesh nodes on a private channel.",
                    style = MaterialTheme.typography.bodyMedium
                )
                
                // Enable/disable switch
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = "Enable Tracking",
                        style = MaterialTheme.typography.bodyLarge,
                        fontWeight = FontWeight.Medium
                    )
                    Switch(
                        checked = enabled,
                        onCheckedChange = onEnabledChange
                    )
                }
                
                if (enabled) {
                    Divider()
                    
                    Text(
                        text = "Select Channel",
                        style = MaterialTheme.typography.bodyMedium,
                        fontWeight = FontWeight.Medium
                    )
                    
                    if (channels.isEmpty()) {
                        Text(
                            text = "No private channels available. Create a private channel first.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.error
                        )
                    } else {
                        Column(
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            channels.forEach { channel ->
                                Card(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(vertical = 4.dp)
                                        .clickable {
                                            onChannelChange(channel.hash.toString())
                                        },
                                    colors = CardDefaults.cardColors(
                                        containerColor = if (selectedChannelHash == channel.hash.toString()) {
                                            MaterialTheme.colorScheme.primaryContainer
                                        } else {
                                            MaterialTheme.colorScheme.surface
                                        }
                                    )
                                ) {
                                    Row(
                                        modifier = Modifier
                                            .fillMaxWidth()
                                            .padding(12.dp),
                                        horizontalArrangement = Arrangement.SpaceBetween,
                                        verticalAlignment = Alignment.CenterVertically
                                    ) {
                                        Column(modifier = Modifier.weight(1f)) {
                                            Text(
                                                text = channel.name,
                                                style = MaterialTheme.typography.bodyMedium,
                                                fontWeight = FontWeight.Medium
                                            )
                                            Text(
                                                text = "Private channel",
                                                style = MaterialTheme.typography.bodySmall,
                                                color = MaterialTheme.colorScheme.onSurfaceVariant
                                            )
                                        }
                                        if (selectedChannelHash == channel.hash.toString()) {
                                            Icon(
                                                imageVector = Icons.Default.Check,
                                                contentDescription = "Selected",
                                                tint = MaterialTheme.colorScheme.primary
                                            )
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text("Done")
            }
        }
    )
}
