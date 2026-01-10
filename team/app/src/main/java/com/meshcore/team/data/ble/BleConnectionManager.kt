package com.meshcore.team.data.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.content.Context
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
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
    
    // Shared flow for incoming messages (hot flow that emits to all collectors)
    private val _incomingMessages = MutableSharedFlow<ChannelMessage>(extraBufferCapacity = 10)
    val incomingMessages: SharedFlow<ChannelMessage> = _incomingMessages.asSharedFlow()
    
    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    Timber.i("Connected to GATT server")
                    
                    // Check if device is bonded (paired)
                    val device = gatt.device
                    when (device.bondState) {
                        BluetoothDevice.BOND_NONE -> {
                            Timber.i("Device not bonded - pairing will be triggered on first encrypted operation")
                        }
                        BluetoothDevice.BOND_BONDING -> {
                            Timber.i("Device bonding in progress...")
                        }
                        BluetoothDevice.BOND_BONDED -> {
                            Timber.i("Device already bonded - encrypted connection ready")
                        }
                    }
                    
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
                Timber.d("Received ${value.size} bytes from TX characteristic: ${value.joinToString(" ") { "%02X".format(it) }}")
                
                // Decode response code
                if (value.isNotEmpty()) {
                    val responseCode = value[0].toInt() and 0xFF
                    val responseNames = mapOf(
                        0 to "OK", 1 to "ERR", 2 to "CONTACTS_START", 3 to "CONTACT",
                        4 to "END_OF_CONTACTS", 5 to "SELF_INFO", 6 to "SENT",
                        10 to "NO_MORE_MESSAGES", 16 to "CONTACT_MSG_RECV_V3",
                        17 to "CHANNEL_MSG_RECV_V3", 18 to "CHANNEL_INFO",
                        0x82 to "PUSH_SEND_CONFIRMED", 0x83 to "PUSH_MSG_WAITING"
                    )
                    Timber.i("Response code: $responseCode (${responseNames[responseCode] ?: "UNKNOWN"})")
                    
                    // Parse and emit channel messages
                    if (responseCode == 17) { // RESP_CODE_CHANNEL_MSG_RECV_V3
                        val message = ResponseParser.parseChannelMessage(value)
                        if (message != null) {
                            Timber.i("📩 Incoming message: '${message.text}'")
                            _incomingMessages.tryEmit(message)
                        }
                    }
                }
                
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
            Timber.e("Cannot send data - not connected (state: ${_connectionState.value})")
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
        
        Timber.d("Sending ${data.size} bytes to RX characteristic: ${data.take(20).joinToString(" ") { "%02X".format(it) }}")
        
        return try {
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
                // Android 13+ (API 33+)
                val result = bluetoothGatt?.writeCharacteristic(
                    rxCharacteristic!!,
                    data,
                    BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                )
                Timber.d("Write result: $result")
                result == BluetoothGatt.GATT_SUCCESS
            } else {
                // Older Android versions
                rxCharacteristic?.value = data
                val result = bluetoothGatt?.writeCharacteristic(rxCharacteristic)
                Timber.d("Write initiated: $result")
                result ?: false
            }
        } catch (e: Exception) {
            Timber.e(e, "Exception during BLE write")
            false
        }
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
