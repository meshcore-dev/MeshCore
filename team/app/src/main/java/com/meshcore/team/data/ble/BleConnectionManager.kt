package com.meshcore.team.data.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import timber.log.Timber
import java.util.UUID

/**
 * Connection states for BLE device
 */
enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING,
    ERROR
}

/**
 * Manages BLE connection to MeshCore companion radio
 * Handles connection lifecycle, characteristic discovery, and notifications
 */
class BleConnectionManager(private val context: Context) {
    
    private var bluetoothGatt: BluetoothGatt? = null
    private var rxCharacteristic: BluetoothGattCharacteristic? = null
    private var txCharacteristic: BluetoothGattCharacteristic? = null
    
    private val _connectionState = MutableStateFlow(ConnectionState.DISCONNECTED)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()
    
    private val _receivedData = MutableStateFlow<ByteArray?>(null)
    val receivedData: StateFlow<ByteArray?> = _receivedData.asStateFlow()
    
    private val _connectedDevice = MutableStateFlow<BluetoothDevice?>(null)
    val connectedDevice: StateFlow<BluetoothDevice?> = _connectedDevice.asStateFlow()
    
    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    Timber.i("Connected to GATT server")
                    _connectionState.value = ConnectionState.CONNECTED
                    _connectedDevice.value = gatt.device
                    // Discover services
                    gatt.discoverServices()
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    Timber.i("Disconnected from GATT server")
                    _connectionState.value = ConnectionState.DISCONNECTED
                    _connectedDevice.value = null
                    cleanup()
                }
            }
        }
        
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Timber.i("Services discovered")
                
                // Find MeshCore service
                val service = gatt.getService(UUID.fromString(BleConstants.SERVICE_UUID))
                if (service != null) {
                    Timber.i("Found MeshCore service")
                    
                    // Get RX and TX characteristics
                    rxCharacteristic = service.getCharacteristic(
                        UUID.fromString(BleConstants.RX_CHARACTERISTIC_UUID)
                    )
                    txCharacteristic = service.getCharacteristic(
                        UUID.fromString(BleConstants.TX_CHARACTERISTIC_UUID)
                    )
                    
                    if (rxCharacteristic != null && txCharacteristic != null) {
                        Timber.i("Found RX and TX characteristics")
                        // Enable notifications on TX characteristic
                        enableNotifications(gatt, txCharacteristic!!)
                    } else {
                        Timber.e("RX or TX characteristic not found")
                        _connectionState.value = ConnectionState.ERROR
                    }
                } else {
                    Timber.e("MeshCore service not found")
                    _connectionState.value = ConnectionState.ERROR
                }
            } else {
                Timber.e("Service discovery failed with status: $status")
                _connectionState.value = ConnectionState.ERROR
            }
        }
        
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            if (characteristic.uuid == UUID.fromString(BleConstants.TX_CHARACTERISTIC_UUID)) {
                Timber.d("Received ${value.size} bytes from TX characteristic")
                _receivedData.value = value
            }
        }
        
        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Timber.d("Characteristic write successful")
            } else {
                Timber.e("Characteristic write failed with status: $status")
            }
        }
        
        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Timber.d("Descriptor write successful - notifications enabled")
            } else {
                Timber.e("Descriptor write failed with status: $status")
            }
        }
    }
    
    /**
     * Connect to a BLE device
     */
    @SuppressLint("MissingPermission")
    fun connect(device: BluetoothDevice) {
        if (_connectionState.value == ConnectionState.CONNECTED || 
            _connectionState.value == ConnectionState.CONNECTING) {
            Timber.w("Already connected or connecting")
            return
        }
        
        Timber.i("Connecting to device: ${device.name} [${device.address}]")
        _connectionState.value = ConnectionState.CONNECTING
        
        bluetoothGatt = device.connectGatt(context, false, gattCallback)
    }
    
    /**
     * Disconnect from the current device
     */
    @SuppressLint("MissingPermission")
    fun disconnect() {
        if (bluetoothGatt == null) {
            return
        }
        
        Timber.i("Disconnecting from device")
        _connectionState.value = ConnectionState.DISCONNECTING
        bluetoothGatt?.disconnect()
    }
    
    /**
     * Send data to the device via RX characteristic
     */
    @SuppressLint("MissingPermission")
    fun sendData(data: ByteArray): Boolean {
        if (_connectionState.value != ConnectionState.CONNECTED) {
            Timber.e("Cannot send data - not connected")
            return false
        }
        
        if (rxCharacteristic == null) {
            Timber.e("RX characteristic not available")
            return false
        }
        
        if (data.size > BleConstants.MAX_FRAME_SIZE) {
            Timber.e("Data size ${data.size} exceeds max frame size ${BleConstants.MAX_FRAME_SIZE}")
            return false
        }
        
        Timber.d("Sending ${data.size} bytes to RX characteristic")
        rxCharacteristic?.value = data
        return bluetoothGatt?.writeCharacteristic(rxCharacteristic) ?: false
    }
    
    /**
     * Enable notifications on TX characteristic
     */
    @SuppressLint("MissingPermission")
    private fun enableNotifications(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
        // Enable local notifications
        gatt.setCharacteristicNotification(characteristic, true)
        
        // Enable remote notifications via descriptor
        val descriptor = characteristic.getDescriptor(
            UUID.fromString("00002902-0000-1000-8000-00805f9b34fb") // Client Characteristic Configuration
        )
        if (descriptor != null) {
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            gatt.writeDescriptor(descriptor)
        } else {
            Timber.e("CCCD descriptor not found")
        }
    }
    
    /**
     * Clean up resources
     */
    @SuppressLint("MissingPermission")
    private fun cleanup() {
        bluetoothGatt?.close()
        bluetoothGatt = null
        rxCharacteristic = null
        txCharacteristic = null
    }
}
