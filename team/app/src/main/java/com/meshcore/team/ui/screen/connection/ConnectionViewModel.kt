package com.meshcore.team.ui.screen.connection

import android.bluetooth.le.ScanResult
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.meshcore.team.data.ble.BleConnectionManager
import com.meshcore.team.data.ble.BleScanner
import com.meshcore.team.data.ble.ConnectionState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
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
