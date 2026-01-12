package com.meshcore.team

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.Map
import androidx.compose.material.icons.filled.Message
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.meshcore.team.data.ble.BleConnectionManager
import com.meshcore.team.data.ble.BleScanner
import com.meshcore.team.data.database.AppDatabase
import com.meshcore.team.data.preferences.AppPreferences
import com.meshcore.team.data.repository.ChannelRepository
import com.meshcore.team.data.repository.MessageRepository
import com.meshcore.team.data.service.TelemetryManager
import com.meshcore.team.service.MeshConnectionService
import com.meshcore.team.ui.screen.connection.ConnectionScreen
import com.meshcore.team.ui.screen.connection.ConnectionViewModel
import com.meshcore.team.ui.screen.map.MapScreen
import com.meshcore.team.ui.screen.map.MapViewModel
import com.meshcore.team.ui.screen.messages.MessageScreen
import com.meshcore.team.ui.screen.messages.MessageViewModel
import com.meshcore.team.ui.theme.TEAMTheme
import kotlinx.coroutines.launch
import timber.log.Timber

/**
 * Main Activity for TEAM app
 * Phase 1: BLE connection setup with permission handling
 */
class MainActivity : ComponentActivity() {
    
    private lateinit var bleScanner: BleScanner
    private lateinit var connectionManager: BleConnectionManager
    private lateinit var connectionViewModel: ConnectionViewModel
    private lateinit var messageViewModel: MessageViewModel
    private lateinit var mapViewModel: MapViewModel
    private lateinit var telemetryManager: TelemetryManager
    private var permissionsGranted by mutableStateOf(false)
    private var currentScreen by mutableStateOf("messages") // Start at messages screen
    
    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        permissionsGranted = permissions.values.all { it }
        if (permissionsGranted) {
            Timber.i("All permissions granted")
        } else {
            Timber.w("Some permissions denied")
        }
    }
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        Timber.i("🏁 MainActivity.onCreate() called - Build: SDK_INT=${Build.VERSION.SDK_INT}")
        
        // Initialize BLE components
        bleScanner = BleScanner(this)
        connectionManager = BleConnectionManager.getInstance(this)
        
        // Initialize AppPreferences
        val appPreferences = AppPreferences(applicationContext)
        
        // Initialize database and repositories
        val database = AppDatabase.getDatabase(applicationContext)
        val messageRepository = MessageRepository(
            database.messageDao(),
            database.ackRecordDao()
        )
        val channelRepository = ChannelRepository(database.channelDao(), connectionManager)
        val contactRepository = com.meshcore.team.data.repository.ContactRepository(
            database.nodeDao(),
            connectionManager
        )
        
        connectionViewModel = ConnectionViewModel(bleScanner, connectionManager, appPreferences, channelRepository, contactRepository)
        
        messageViewModel = MessageViewModel(
            messageRepository, 
            channelRepository, 
            contactRepository, 
            connectionManager,
            appPreferences
        )
        
        // Initialize MapViewModel
        mapViewModel = MapViewModel(
            applicationContext,
            contactRepository,
            appPreferences
        )
        
        // Initialize TelemetryManager (handles automatic location tracking)
        telemetryManager = TelemetryManager(
            applicationContext,
            appPreferences,
            lifecycleScope
        )
        
        // Connect MapViewModel location updates to telemetry
        lifecycleScope.launch {
            mapViewModel.currentLocation.collect { location ->
                location?.let {
                    telemetryManager.sendLocationUpdate(it.latitude, it.longitude)
                }
            }
        }
        
        // Start background service for persistent mesh connection
        MeshConnectionService.startService(this)
        Timber.i("🔄 Background service started")
        
        // Request permissions
        requestBluetoothPermissions()
        
        setContent {
            TEAMTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    if (permissionsGranted) {
                        MainScreenWithNavigation(
                            currentScreen = currentScreen,
                            onScreenChange = { currentScreen = it },
                            connectionViewModel = connectionViewModel,
                            messageViewModel = messageViewModel,
                            mapViewModel = mapViewModel
                        )
                    } else {
                        PermissionRequiredScreen(
                            onRequestPermissions = { requestBluetoothPermissions() }
                        )
                    }
                }
            }
        }
    }
    
    private fun requestBluetoothPermissions() {
        val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.ACCESS_FINE_LOCATION
            )
        } else {
            arrayOf(
                Manifest.permission.BLUETOOTH,
                Manifest.permission.BLUETOOTH_ADMIN,
                Manifest.permission.ACCESS_FINE_LOCATION
            )
        }
        
        val allGranted = permissions.all { permission ->
            ContextCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED
        }
        
        if (allGranted) {
            permissionsGranted = true
        } else {
            permissionLauncher.launch(permissions)
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreenWithNavigation(
    currentScreen: String,
    onScreenChange: (String) -> Unit,
    connectionViewModel: ConnectionViewModel,
    messageViewModel: MessageViewModel,
    mapViewModel: MapViewModel
) {
    Scaffold(
        bottomBar = {
            NavigationBar {
                NavigationBarItem(
                    selected = currentScreen == "messages",
                    onClick = { onScreenChange("messages") },
                    icon = { Icon(Icons.Default.Message, "Messages") },
                    label = { Text("Messages") }
                )
                NavigationBarItem(
                    selected = currentScreen == "map",
                    onClick = { onScreenChange("map") },
                    icon = { Icon(Icons.Default.Map, "Map") },
                    label = { Text("Map") }
                )
                NavigationBarItem(
                    selected = currentScreen == "connection",
                    onClick = { onScreenChange("connection") },
                    icon = { Icon(Icons.Default.Bluetooth, "Connection") },
                    label = { Text("Connection") }
                )
            }
        }
    ) { padding ->
        Box(modifier = Modifier.padding(padding)) {
            when (currentScreen) {
                "messages" -> MessageScreen(
                    viewModel = messageViewModel,
                    onNavigateToConnection = { onScreenChange("connection") }
                )
                "map" -> MapScreen(
                    viewModel = mapViewModel
                )
                "connection" -> ConnectionScreen(
                    viewModel = connectionViewModel
                )
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PermissionRequiredScreen(onRequestPermissions: () -> Unit) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("TEAM") }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            Text(
                text = "Permissions Required",
                style = MaterialTheme.typography.headlineMedium
            )
            
            Spacer(modifier = Modifier.height(16.dp))
            
            Text(
                text = "TEAM requires Bluetooth and Location permissions to scan for and connect to MeshCore devices.",
                style = MaterialTheme.typography.bodyMedium
            )
            
            Spacer(modifier = Modifier.height(24.dp))
            
            Button(onClick = onRequestPermissions) {
                Text("Grant Permissions")
            }
        }
    }
}
