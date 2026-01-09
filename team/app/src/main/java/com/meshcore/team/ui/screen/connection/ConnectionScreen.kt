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
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
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
