package com.meshcore.team.ui.screen.connection

import android.bluetooth.le.ScanResult
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.meshcore.team.data.ble.BleConnectionManager
import com.meshcore.team.data.ble.BleScanner
import com.meshcore.team.data.ble.CommandSerializer
import com.meshcore.team.data.ble.ConnectionState
import com.meshcore.team.data.database.ChannelEntity
import com.meshcore.team.data.preferences.AppPreferences
import com.meshcore.team.data.repository.ChannelRepository
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import timber.log.Timber

/**
 * ViewModel for BLE connection screen
 */
class ConnectionViewModel(
    private val bleScanner: BleScanner,
    private val connectionManager: BleConnectionManager,
    private val appPreferences: AppPreferences,
    private val channelRepository: ChannelRepository,
    private val contactRepository: com.meshcore.team.data.repository.ContactRepository
) : ViewModel() {
    
    private val _scannedDevices = MutableStateFlow<List<ScanResult>>(emptyList())
    val scannedDevices: StateFlow<List<ScanResult>> = _scannedDevices.asStateFlow()
    
    private val _isScanning = MutableStateFlow(false)
    val isScanning: StateFlow<Boolean> = _isScanning.asStateFlow()
    
    val connectionState: StateFlow<ConnectionState> = connectionManager.connectionState
    val connectedDevice = connectionManager.connectedDevice
    
    // Location source preference
    val locationSource: StateFlow<String> = appPreferences.locationSource
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(5000),
            initialValue = AppPreferences.LOCATION_SOURCE_PHONE
        )
    
    // User alias preference
    val userAlias: StateFlow<String?> = appPreferences.userAlias
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(5000),
            initialValue = null
        )
    
    // Telemetry enabled state
    val telemetryEnabled: StateFlow<Boolean> = appPreferences.telemetryEnabled
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(5000),
            initialValue = false
        )
    
    // Telemetry channel hash
    val telemetryChannelHash: StateFlow<String?> = appPreferences.telemetryChannelHash
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(5000),
            initialValue = null
        )
    
    // Device name from firmware (SELF_INFO)
    private val _deviceName = MutableStateFlow("Unknown Device")
    val deviceName: StateFlow<String> = _deviceName.asStateFlow()
    
    // All channels (for telemetry channel selection)
    val channels: StateFlow<List<ChannelEntity>> = channelRepository.getAllChannels()
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(5000),
            initialValue = emptyList()
        )
    
    private var messagePollingJob: Job? = null
    
    init {
        // Monitor device info responses to get actual firmware device name
        viewModelScope.launch {
            connectionManager.deviceInfo.collect { selfInfo ->
                Timber.i("📝 Received device name from firmware: '${selfInfo.name}' (was: '${_deviceName.value}')")
                _deviceName.value = selfInfo.name
                Timber.i("📝 Device name updated in UI to: '${_deviceName.value}'")
            }
        }
        
        // Monitor connection state and send CMD_APP_START when connected
        viewModelScope.launch {
            connectionManager.connectionState.collect { state ->
                if (state == ConnectionState.CONNECTED) {
                    // Stop scanning when connected to save battery
                    if (_isScanning.value) {
                        Timber.i("🔍 Stopping scan - device connected")
                        stopScan()
                    }
                    
                    // Wait a bit for BLE to stabilize
                    delay(500)
                    // Send CMD_APP_START to initialize the companion radio session
                    val appStartCmd = CommandSerializer.appStart()
                    val success = connectionManager.sendData(appStartCmd)
                    if (success) {
                        Timber.i("✓ CMD_APP_START sent - companion radio initialized")
                        // Device name will be updated when SELF_INFO response is received
                        // Start polling for incoming messages
                        startMessagePolling()
                    } else {
                        Timber.e("✗ Failed to send CMD_APP_START")
                    }
                } else {
                    // Stop polling when disconnected
                    _deviceName.value = "Unknown Device"
                    stopMessagePolling()
                }
            }
        }
    }
    
    /**
     * Start polling for incoming messages
     */
    private fun startMessagePolling() {
        stopMessagePolling() // Cancel any existing polling
        
        messagePollingJob = viewModelScope.launch {
            Timber.d("Starting message polling")
            while (isActive && connectionManager.connectionState.value == ConnectionState.CONNECTED) {
                // Poll for next message
                val syncCmd = CommandSerializer.syncNextMessage()
                connectionManager.sendData(syncCmd)
                
                // Poll every 2 seconds
                delay(2000)
            }
            Timber.d("Message polling stopped")
        }
    }
    
    /**
     * Stop polling for messages
     */
    private fun stopMessagePolling() {
        messagePollingJob?.cancel()
        messagePollingJob = null
    }
    
    /**
     * Start scanning for devices
     */
    fun startScan() {
        if (!bleScanner.isBluetoothAvailable()) {
            Timber.e("Bluetooth not available")
            return
        }
        
        if (!bleScanner.isBluetoothEnabled()) {
            Timber.e("Bluetooth not enabled")
            return
        }
        
        _isScanning.value = true
        _scannedDevices.value = emptyList()
        
        viewModelScope.launch {
            bleScanner.scanForDevices().collect { scanResult ->
                val currentDevices = _scannedDevices.value.toMutableList()
                
                // Update or add device (avoid duplicates by address)
                val existingIndex = currentDevices.indexOfFirst { 
                    it.device.address == scanResult.device.address 
                }
                
                if (existingIndex >= 0) {
                    currentDevices[existingIndex] = scanResult
                } else {
                    currentDevices.add(scanResult)
                }
                
                _scannedDevices.value = currentDevices
            }
        }
    }
    
    /**
     * Stop scanning
     */
    fun stopScan() {
        _isScanning.value = false
    }
    
    /**
     * Connect to a device
     */
    fun connect(scanResult: ScanResult) {
        stopScan()
        connectionManager.connect(scanResult.device)
    }
    
    /**
     * Disconnect from current device
     */
    fun disconnect() {
        connectionManager.disconnect()
    }
    
    /**
     * Set the user alias (display name for messages and telemetry)
     * This is stored locally and doesn't change the device's actual name
     */
    fun setUserAlias(alias: String) {
        viewModelScope.launch {
            appPreferences.setUserAlias(alias)
            Timber.i("User alias updated to: '$alias'")
        }
    }
    
    /**
     * Set the location source preference
     */
    fun setLocationSource(source: String) {
        viewModelScope.launch {
            appPreferences.setLocationSource(source)
            Timber.i("Location source updated to: $source")
        }
    }
    
    /**
     * Enable or disable telemetry tracking
     */
    fun setTelemetryEnabled(enabled: Boolean) {
        viewModelScope.launch {
            appPreferences.setTelemetryEnabled(enabled)
            Timber.i("Telemetry ${if (enabled) "enabled" else "disabled"}")
        }
    }
    
    /**
     * Set the channel for telemetry messages
     */
    fun setTelemetryChannel(channelHash: String?) {
        viewModelScope.launch {
            appPreferences.setTelemetryChannelHash(channelHash)
            channelHash?.let { hash ->
                val channel = channels.value.find { it.hash.toString() == hash }
                Timber.i("Telemetry channel set to: ${channel?.name ?: "Unknown"}")
            } ?: Timber.i("Telemetry channel cleared")
        }
    }
    
    /**
     * Clear all contacts from database
     */
    fun clearAllContacts() {
        viewModelScope.launch {
            try {
                contactRepository.getNodeDao().deleteAllNodes()
                Timber.i("✓ All contacts cleared from database")
            } catch (e: Exception) {
                Timber.e(e, "❌ Failed to clear contacts")
            }
        }
    }
    
    override fun onCleared() {
        super.onCleared()
        stopScan()
    }
}
