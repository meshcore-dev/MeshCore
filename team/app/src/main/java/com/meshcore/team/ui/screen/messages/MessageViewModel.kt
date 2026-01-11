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
                .onEach { messages ->
                    Timber.d("Messages Flow emitted: ${messages.size} messages, statuses=${messages.map { it.deliveryStatus }}")
                }
        }
        .stateIn(viewModelScope, SharingStarted.Eagerly, emptyList())
    
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
        
        // Monitor BLE connection state to sync channels when connected
        viewModelScope.launch {
            bleConnectionManager.connectionState.collect { state ->
                if (state == ConnectionState.CONNECTED) {
                    Timber.i("🔗 BLE Connected - syncing channels with firmware...")
                    kotlinx.coroutines.delay(2000) // Wait for connection setup to complete (CMD_APP_START, polling, etc.)
                    channelRepository.syncAllChannelsWithFirmware()
                }
            }
        }
        
        // Listen for incoming messages from BLE
        viewModelScope.launch {
            bleConnectionManager.incomingMessages.collect { channelMessage ->
                Timber.i("Processing incoming message: text='${channelMessage.text}', length=${channelMessage.text.length}")
                
                // Parse MeshCore format: "SENDERID: MESSAGE"
                // The firmware prepends sender name/ID with colon separator
                val (senderName, messageContent) = if (channelMessage.text.contains(": ")) {
                    val parts = channelMessage.text.split(": ", limit = 2)
                    Pair(parts[0], parts.getOrNull(1) ?: "")
                } else {
                    // Fallback if no colon separator found
                    Pair("Mesh User", channelMessage.text)
                }
                
                Timber.d("Parsed message: sender='$senderName', content='$messageContent'")
                
                // Look up channel by the index received from firmware
                val channel = channelRepository.getChannelByIndex(channelMessage.channelIdx)
                
                if (channel != null) {
                    // Save message to database
                    val messageEntity = MessageEntity(
                        id = java.util.UUID.randomUUID().toString(),
                        channelHash = channel.hash,
                        senderId = ByteArray(32), // Unknown sender - use empty array
                        senderName = senderName,
                        content = messageContent,
                        timestamp = channelMessage.timestamp * 1000, // Convert to milliseconds
                        isPrivate = !channel.isPublic,
                        ackChecksum = null,
                        deliveryStatus = "RECEIVED",
                        heardByCount = 0,
                        attempt = 0,
                        isSentByMe = false
                    )
                    
                    messageRepository.insertMessage(messageEntity)
                    Timber.i("✓ Incoming message saved to channel '${channel.name}' (idx=${channelMessage.channelIdx}): content='${messageEntity.content}', sender='${messageEntity.senderName}'")
                } else {
                    Timber.w("No channel found for index ${channelMessage.channelIdx} - message dropped")
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
        
        // Listen for SEND_CONFIRMED (ACK) notifications
        viewModelScope.launch {
            bleConnectionManager.sendConfirmed.collect { ack ->
                Timber.i("🎯 Processing ACK: checksum=${ack.ackChecksum.joinToString("") { "%02X".format(it) }}, RTT=${ack.roundTripTimeMs}ms")
                
                // Find message with matching checksum
                val allMessages = messageRepository.getAllMessages().first()
                Timber.d("Searching ${allMessages.size} messages for matching checksum")
                
                // Debug: print all message checksums
                allMessages.filter { it.ackChecksum != null }.forEach { msg ->
                    Timber.d("  Message ${msg.id}: checksum=${msg.ackChecksum?.joinToString("") { "%02X".format(it) }}, status=${msg.deliveryStatus}, count=${msg.heardByCount}")
                }
                
                val matchingMessage = allMessages.firstOrNull { msg ->
                    msg.ackChecksum?.contentEquals(ack.ackChecksum) == true
                }
                
                if (matchingMessage != null) {
                    Timber.i("✅ ACK matched message: ${matchingMessage.id}, content='${matchingMessage.content.take(20)}...', current status=${matchingMessage.deliveryStatus}, count=${matchingMessage.heardByCount}")
                    
                    // Record the ACK
                    // TODO: We don't know which device sent the ACK yet (firmware limitation)
                    // For now, use a placeholder public key
                    val placeholderDevicePubKey = ByteArray(32) { 0xFF.toByte() }
                    
                    // Assume direct contact for now (isDirect = true) since we don't have path info
                    messageRepository.recordAck(
                        messageId = matchingMessage.id,
                        ackChecksum = ack.ackChecksum,
                        devicePublicKey = placeholderDevicePubKey,
                        roundTripTimeMs = ack.roundTripTimeMs,
                        isDirect = true  // TODO: Determine from path length when available
                    )
                    
                    Timber.i("✅ ACK processing complete for message ${matchingMessage.id}")
                } else {
                    Timber.w("⚠️ No message found matching ACK checksum: ${ack.ackChecksum.joinToString("") { "%02X".format(it) }}")
                    Timber.w("   This could mean: 1) Message not sent yet, 2) Checksum mismatch, or 3) ACK for someone else's message")
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
     * Create a new private channel
     */
    fun createPrivateChannel(name: String) {
        viewModelScope.launch {
            val channel = channelRepository.createPrivateChannel(name)
            
            // Register channel with companion radio firmware
            val registered = channelRepository.registerChannelWithFirmware(channel)
            if (registered) {
                _selectedChannel.value = channel
                Timber.i("Created and registered private channel: ${channel.name}")
            } else {
                Timber.e("Failed to register channel with firmware - messages may not work")
                _selectedChannel.value = channel // Still switch to it
            }
        }
    }
    
    /**
     * Import a channel from shared key
     */
    fun importChannel(name: String, pskBase64: String, onResult: (Boolean) -> Unit) {
        viewModelScope.launch {
            val channel = channelRepository.importChannel(name, pskBase64)
            if (channel != null) {
                // Register channel with companion radio firmware
                val registered = channelRepository.registerChannelWithFirmware(channel)
                if (registered) {
                    _selectedChannel.value = channel
                    Timber.i("Imported and registered channel: ${channel.name}")
                    onResult(true)
                } else {
                    Timber.e("Failed to register channel with firmware")
                    onResult(false)
                }
            } else {
                Timber.e("Failed to import channel: invalid PSK")
                onResult(false)
            }
        }
    }
    
    /**
     * Get shareable channel key
     */
    fun getChannelShareKey(channel: ChannelEntity): String {
        return channelRepository.exportChannelKey(channel)
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
                    
                    // Calculate ACK checksum for tracking
                    val ackChecksum = com.meshcore.team.data.ble.AckChecksumCalculator.calculateChecksum(
                        timestamp = timestamp,
                        attempt = 0,  // First attempt
                        text = content,
                        senderPubKey = mockSenderId
                    )
                    
                    Timber.d("Calculated ACK checksum: ${ackChecksum.joinToString("") { "%02X".format(it) }}")
                    
                    // Store checksum in message for later correlation
                    val updatedMessage = message.copy(ackChecksum = ackChecksum)
                    messageRepository.updateMessage(updatedMessage)
                    
                    val command = if (channel.isPublic) {
                        CommandSerializer.sendChannelTextMessage(channel.channelIndex, timestamp, content)
                    } else {
                        // Private channels also use channel index
                        CommandSerializer.sendChannelTextMessage(channel.channelIndex, timestamp, content)
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
