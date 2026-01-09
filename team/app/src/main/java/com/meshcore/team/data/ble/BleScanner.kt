package com.meshcore.team.data.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.ParcelUuid
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import timber.log.Timber
import java.util.UUID

/**
 * BLE Scanner for discovering MeshCore companion radios
 * Filters devices by MeshCore service UUID
 */
class BleScanner(private val context: Context) {
    
    private val bluetoothManager: BluetoothManager = 
        context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager.adapter
    
    private val bleScanner: BluetoothLeScanner? = bluetoothAdapter?.bluetoothLeScanner
    
    /**
     * Start scanning for MeshCore devices
     * Returns a Flow of discovered devices
     */
    @SuppressLint("MissingPermission")
    fun scanForDevices(): Flow<ScanResult> = callbackFlow {
        if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled) {
            Timber.e("Bluetooth is not available or not enabled")
            close()
            return@callbackFlow
        }
        
        if (bleScanner == null) {
            Timber.e("BLE scanner is not available")
            close()
            return@callbackFlow
        }
        
        val scanCallback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                Timber.d("Found device: ${result.device.name} [${result.device.address}]")
                trySend(result)
            }
            
            override fun onBatchScanResults(results: List<ScanResult>) {
                results.forEach { result ->
                    Timber.d("Found device (batch): ${result.device.name} [${result.device.address}]")
                    trySend(result)
                }
            }
            
            override fun onScanFailed(errorCode: Int) {
                Timber.e("BLE scan failed with error code: $errorCode")
                close()
            }
        }
        
        // Build scan filter for MeshCore service UUID
        val scanFilter = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid(UUID.fromString(BleConstants.SERVICE_UUID)))
            .build()
        
        // Configure scan settings for low latency
        val scanSettings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        
        Timber.i("Starting BLE scan for MeshCore devices")
        bleScanner.startScan(listOf(scanFilter), scanSettings, scanCallback)
        
        awaitClose {
            Timber.i("Stopping BLE scan")
            bleScanner.stopScan(scanCallback)
        }
    }
    
    /**
     * Check if Bluetooth is enabled
     */
    fun isBluetoothEnabled(): Boolean {
        return bluetoothAdapter?.isEnabled == true
    }
    
    /**
     * Check if Bluetooth is available on this device
     */
    fun isBluetoothAvailable(): Boolean {
        return bluetoothAdapter != null
    }
}
