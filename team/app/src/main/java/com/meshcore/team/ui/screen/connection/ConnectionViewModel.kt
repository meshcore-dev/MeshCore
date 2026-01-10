package com.meshcore.team.ui.screen.connection

import android.bluetooth.le.ScanResult
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.meshcore.team.data.ble.BleConnectionManager
import com.meshcore.team.data.ble.BleScanner
import com.meshcore.team.data.ble.CommandSerializer
import com.meshcore.team.data.ble.ConnectionState
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import timber.log.Timber

/**
 * ViewModel for BLE connection screen
 */
class ConnectionViewModel(
    private val bleScanner: BleScanner,
    private val connectionManager: BleConnectionManager
) : ViewModel() {
    
    private val _scannedDevices = MutableStateFlow<List<ScanResult>>(emptyList())
    val scannedDevices: StateFlow<List<ScanResult>> = _scannedDevices.asStateFlow()
    
    private val _isScanning = MutableStateFlow(false)
    val isScanning: StateFlow<Boolean> = _isScanning.asStateFlow()
    
    val connectionState: StateFlow<ConnectionState> = connectionManager.connectionState
    val connectedDevice = connectionManager.connectedDevice
    
    private var messagePollingJob: Job? = null
    
    init {
        // Monitor connection state and send CMD_APP_START when connected
        viewModelScope.launch {
            connectionManager.connectionState.collect { state ->
                if (state == ConnectionState.CONNECTED) {
                    // Wait a bit for BLE to stabilize
                    delay(500)
                    // Send CMD_APP_START to initialize the companion radio session
                    val appStartCmd = CommandSerializer.appStart()
                    val success = connectionManager.sendData(appStartCmd)
                    if (success) {
                        Timber.i("✓ CMD_APP_START sent - companion radio initialized")
                        // Start polling for incoming messages
                        startMessagePolling()
                    } else {
                        Timber.e("✗ Failed to send CMD_APP_START")
                    }
                } else {
                    // Stop polling when disconnected
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
    
    override fun onCleared() {
        super.onCleared()
        stopScan()
    }
}
