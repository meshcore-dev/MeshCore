package com.meshcore.team.ui.screen.messages

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.meshcore.team.data.ble.BleConnectionManager
import com.meshcore.team.data.ble.BleConstants
import com.meshcore.team.data.ble.CommandSerializer
import com.meshcore.team.data.ble.ConnectionState
import com.meshcore.team.data.database.ChannelEntity
import com.meshcore.team.data.database.MessageEntity
import com.meshcore.team.data.repository.ChannelRepository
import com.meshcore.team.data.repository.MessageRepository
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch
import timber.log.Timber
import java.util.concurrent.ConcurrentHashMap

/**
 * ViewModel for messaging screen
 */
class MessageViewModel(
    private val messageRepository: MessageRepository,
    private val channelRepository: ChannelRepository,
    private val bleConnectionManager: BleConnectionManager
) : ViewModel() {
    
    private val _selectedChannel = MutableStateFlow<ChannelEntity?>(null)
    val selectedChannel: StateFlow<ChannelEntity?> = _selectedChannel.asStateFlow()
    
    val channels: StateFlow<List<ChannelEntity>> = channelRepository.getAllChannels()
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), emptyList())
    
    val messages: StateFlow<List<MessageEntity>> = _selectedChannel
        .filterNotNull()
        .flatMapLatest { channel ->
            messageRepository.getMessagesByChannel(channel.hash)
        }
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), emptyList())
    
    val connectionState: StateFlow<ConnectionState> = bleConnectionManager.connectionState
    val connectedDevice = bleConnectionManager.connectedDevice
    
    // Track pending messages awaiting SENT confirmation
    private val pendingSentConfirmations = ConcurrentHashMap<String, String>() // messageId -> messageId
    
    init {
        // Initialize default public channel
        viewModelScope.launch {
            channelRepository.initializeDefaultChannel()
            
            // Select first channel (public channel)
            channelRepository.getAllChannels().first().firstOrNull()?.let { firstChannel ->
                _selectedChannel.value = firstChannel
            }
        }
        
        // Listen for incoming messages from BLE
        viewModelScope.launch {
            bleConnectionManager.incomingMessages.collect { channelMessage ->
                Timber.i("Processing incoming message: '${channelMessage.text}'")
                
                // Channel idx 0 = public channel (hash 0x11)
                // For now, all messages go to the public channel
                val channel = channelRepository.getAllChannels().first()
                    .firstOrNull { it.isPublic } // Just find the public channel
                
                if (channel != null) {
                    // Save message to database
                    val messageEntity = MessageEntity(
                        id = java.util.UUID.randomUUID().toString(),
                        channelHash = channel.hash,
                        senderId = ByteArray(32), // Unknown sender - use empty array
                        senderName = "Mesh User", // Placeholder
                        content = channelMessage.text,
                        timestamp = channelMessage.timestamp * 1000, // Convert to milliseconds
                        isPrivate = false,
                        ackChecksum = null,
                        deliveryStatus = "RECEIVED",
                        heardByCount = 0,
                        attempt = 0,
                        isSentByMe = false
                    )
                    
                    messageRepository.insertMessage(messageEntity)
                    Timber.i("✓ Incoming message saved to database for channel '${channel.name}'")
                } else {
                    Timber.w("No public channel found for incoming message")
                }
            }
        }
        
        // Monitor for RESP_CODE_OK/SENT confirmations after sending
        viewModelScope.launch {
            bleConnectionManager.receivedData.collect { data ->
                if (data != null && data.isNotEmpty()) {
                    val responseCode = data[0].toInt() and 0xFF
                    // Companion radio returns OK (0) for channel messages, SENT (6) for private messages
                    if (responseCode == BleConstants.Responses.RESP_CODE_OK.toInt() ||
                        responseCode == BleConstants.Responses.RESP_CODE_SENT.toInt()) {
                        // Message was accepted by companion radio for transmission
                        // Update the most recent pending message
                        val pendingIds = pendingSentConfirmations.keys().toList()
                        if (pendingIds.isNotEmpty()) {
                            val messageId = pendingIds.last()
                            pendingSentConfirmations.remove(messageId)
                            
                            messageRepository.updateDeliveryStatus(
                                messageId,
                                com.meshcore.team.data.database.DeliveryStatus.SENT,
                                0
                            )
                            Timber.i("✓ Message $messageId confirmed SENT by companion radio (code=$responseCode)")
                        }
                    }
                }
            }
        }
    }
    
    /**
     * Select a channel
     */
    fun selectChannel(channel: ChannelEntity) {
        _selectedChannel.value = channel
        Timber.d("Selected channel: ${channel.name}")
    }
    
    /**
     * Send a message
     */
    fun sendMessage(content: String) {
        val channel = _selectedChannel.value ?: return
        
        if (content.isBlank()) {
            Timber.w("Cannot send empty message")
            return
        }
        
        viewModelScope.launch {
            // For now, use mock sender data
            // In real implementation, this would come from device identity
            val mockSenderId = ByteArray(32) { it.toByte() }
            val mockSenderName = "Me"
            
            val message = messageRepository.createOutgoingMessage(
                senderId = mockSenderId,
                senderName = mockSenderName,
                channelHash = channel.hash,
                content = content,
                isPrivate = !channel.isPublic
            )
            
            Timber.i("Message created: ${message.id}")
            
            // Send via BLE if connected
            if (bleConnectionManager.connectionState.value == ConnectionState.CONNECTED) {
                try {
                    val timestamp = (System.currentTimeMillis() / 1000).toInt()
                    val command = if (channel.isPublic) {
                        CommandSerializer.sendChannelTextMessage(channel.hash, timestamp, content)
                    } else {
                        // For private messages, we'd need the recipient's public key
                        // For now, just send to public channel
                        CommandSerializer.sendChannelTextMessage(channel.hash, timestamp, content)
                    }
                    
                    Timber.d("Attempting to send ${command.size} byte command via BLE")
                    val success = bleConnectionManager.sendData(command)
                    if (success) {
                        Timber.i("✓ Message written to BLE: ${content.take(20)}...")
                        // Keep in SENDING state, wait for RESP_CODE_SENT
                        pendingSentConfirmations[message.id] = message.id
                    } else {
                        Timber.e("✗ BLE sendData returned false - write failed")
                        // Keep message in SENDING state so user knows it failed
                    }
                } catch (e: Exception) {
                    Timber.e(e, "✗ Exception sending message via BLE")
                }
            } else {
                Timber.w("Not connected to device - message queued locally")
                // Just mark as sent for testing without device
                messageRepository.updateDeliveryStatus(
                    message.id,
                    com.meshcore.team.data.database.DeliveryStatus.SENT,
                    0
                )
            }
        }
    }
    
    /**
     * Toggle location sharing for a channel
     */
    fun toggleLocationSharing(channel: ChannelEntity) {
        if (!channel.isPublic) {
            // Private channels always share location
            Timber.w("Cannot disable location sharing on private channel")
            return
        }
        
        viewModelScope.launch {
            channelRepository.updateLocationSharing(channel.hash, !channel.shareLocation)
            Timber.i("Location sharing toggled for ${channel.name}: ${!channel.shareLocation}")
        }
    }
    
    /**
     * Add mock messages for testing
     */
    fun addMockMessages() {
        val channel = _selectedChannel.value ?: return
        
        viewModelScope.launch {
            val mySenderId = ByteArray(32) { it.toByte() }
            
            // Create messages from different users
            val testUsers = listOf(
                Triple(ByteArray(32) { (it + 10).toByte() }, "Hunter-1", "Hey, where are you?"),
                Triple(mySenderId, "Me", "I'm at the north stand"),
                Triple(ByteArray(32) { (it + 20).toByte() }, "Scout-2", "Copy that, heading your way"),
                Triple(ByteArray(32) { (it + 10).toByte() }, "Hunter-1", "See anything yet?"),
                Triple(mySenderId, "Me", "Negative, nothing on my side"),
                Triple(ByteArray(32) { (it + 30).toByte() }, null, "Radio check from unknown")
            )
            
            testUsers.forEach { (senderId, name, text) ->
                val msg = MessageEntity(
                    id = "mock_${System.currentTimeMillis()}_${text.hashCode()}",
                    senderId = senderId,
                    senderName = name,
                    channelHash = channel.hash,
                    content = text,
                    timestamp = System.currentTimeMillis(),
                    isPrivate = false,
                    ackChecksum = null,
                    deliveryStatus = "SENT",
                    heardByCount = 0,
                    attempt = 0,
                    isSentByMe = senderId.contentEquals(mySenderId)
                )
                messageRepository.insertMessage(msg)
            }
            
            Timber.i("Added mock messages from different senders")
        }
    }
}
