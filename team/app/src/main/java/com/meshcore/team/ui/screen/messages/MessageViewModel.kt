package com.meshcore.team.ui.screen.messages

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.meshcore.team.data.ble.BleConnectionManager
import com.meshcore.team.data.ble.BleConstants
import com.meshcore.team.data.ble.CommandSerializer
import com.meshcore.team.data.ble.ConnectionState
import com.meshcore.team.data.database.ChannelEntity
import com.meshcore.team.data.database.MessageEntity
import com.meshcore.team.data.preferences.AppPreferences
import com.meshcore.team.data.repository.ChannelRepository
import com.meshcore.team.data.repository.MessageRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import timber.log.Timber
import java.util.concurrent.ConcurrentHashMap

/**
 * ViewModel for messaging screen
 */
class MessageViewModel(
    private val messageRepository: MessageRepository,
    private val channelRepository: ChannelRepository,
    private val contactRepository: com.meshcore.team.data.repository.ContactRepository,
    private val bleConnectionManager: BleConnectionManager,
    private val appPreferences: AppPreferences
) : ViewModel() {
    
    private val _selectedChannel = MutableStateFlow<ChannelEntity?>(null)
    val selectedChannel: StateFlow<ChannelEntity?> = _selectedChannel.asStateFlow()
    
    // UI state: "channels" or "contacts" for switching between channel and direct messaging
    private val _messagingMode = MutableStateFlow("channels")
    val messagingMode: StateFlow<String> = _messagingMode.asStateFlow()
    
    // Selected contact for direct messaging
    private val _selectedContact = MutableStateFlow<com.meshcore.team.data.database.NodeEntity?>(null)
    val selectedContact: StateFlow<com.meshcore.team.data.database.NodeEntity?> = _selectedContact.asStateFlow()
    
    val channels: StateFlow<List<ChannelEntity>> = channelRepository.getAllChannels()
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), emptyList())
    
    val contacts: StateFlow<List<com.meshcore.team.data.database.NodeEntity>> = contactRepository.getAllContacts()
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
            
            // Check existing contacts for device ID names that need alias updates
            updateExistingContactsWithAliases()
            
            // Clean up any duplicate contacts
            cleanupDuplicateContacts()
        }
        
        // Monitor BLE connection state to sync channels and contacts when connected
        viewModelScope.launch {
            bleConnectionManager.connectionState.collect { state ->
                if (state == ConnectionState.CONNECTED) {
                    Timber.i("🔗 BLE Connected - syncing channels and contacts with firmware...")
                    kotlinx.coroutines.delay(2000) // Wait for connection setup to complete
                    channelRepository.syncAllChannelsWithFirmware()
                    kotlinx.coroutines.delay(500) // Small delay between sync operations
                    contactRepository.syncContacts()
                }
            }
        }
        
        // Listen for contact responses from BLE
        viewModelScope.launch {
            bleConnectionManager.contactResponses.collect { frame ->
                contactRepository.parseContactResponse(frame)
            }
        }
        
        // Listen for incoming channel messages from BLE
        viewModelScope.launch {
            bleConnectionManager.incomingMessages.collect { channelMessage ->
                Timber.i("Processing incoming channel message: text='${channelMessage.text}', length=${channelMessage.text.length}")
                
                // Parse MeshCore format: "SENDERID: MESSAGE"
                // The firmware prepends sender name/ID with colon separator
                val (initialSenderName, initialMessageContent) = if (channelMessage.text.contains(": ")) {
                    val parts = channelMessage.text.split(": ", limit = 2)
                    Pair(parts[0], parts.getOrNull(1) ?: "")
                } else {
                    // Fallback if no colon separator found
                    Pair("Mesh User", channelMessage.text)
                }
                
                // Check if the message content contains an embedded alias (from our alias-in-message approach)
                // Format: "DeviceID: UserAlias: ActualMessage" -> extract "UserAlias" as real sender
                val (senderName, messageContent) = if (initialMessageContent.contains(": ")) {
                    val aliasParts = initialMessageContent.split(": ", limit = 2)
                    val potentialAlias = aliasParts[0]
                    val actualContent = aliasParts.getOrNull(1) ?: ""
                    
                    // Use alias if it looks like a user-set name (not a device ID pattern)
                    if (potentialAlias.isNotBlank() && !potentialAlias.matches(Regex(".*[0-9A-F]{6,}.*"))) {
                        Timber.d("Found embedded alias: '$potentialAlias' from device '$initialSenderName'")
                        Pair(potentialAlias, actualContent)
                    } else {
                        // No valid alias found, use device name
                        Pair(initialSenderName, initialMessageContent)
                    }
                } else {
                    // No embedded alias, use device name
                    Pair(initialSenderName, initialMessageContent)
                }
                
                Timber.d("Message from '$senderName': '$messageContent'")
                
                // Check if this is a telemetry message
                if (com.meshcore.team.data.model.TelemetryMessage.isTelemetryMessage(messageContent)) {
                    Timber.i("📍 Telemetry message received from '$senderName'")
                    handleTelemetryMessage(senderName, messageContent, channelMessage.pathLen)
                    return@collect // Don't save telemetry messages to chat
                }
                
                // Look up channel by the index received from firmware
                val channel = channelRepository.getChannelByIndex(channelMessage.channelIdx)
                
                if (channel != null) {
                    // Update or create contact with alias name
                    viewModelScope.launch {
                        try {
                            Timber.i("🔍 Searching for contact with name: '$senderName'")
                            val contacts = contactRepository.getAllContacts().first()
                            Timber.i("🔍 Found ${contacts.size} existing contacts: ${contacts.map { it.name }.joinToString(", ")}")
                            
                            var contact = contacts.firstOrNull { it.name == senderName }
                            
                            if (contact == null) {
                                // Try partial match for existing contacts with different names
                                Timber.i("🔍 No exact match, trying partial matches for '$senderName'...")
                                contact = contacts.firstOrNull { existingContact ->
                                    val existingName = existingContact.name
                                    if (existingName != null) {
                                        // Check if existing name looks like a device ID and sender is an alias
                                        val isExistingDeviceId = existingName.contains("MeshCore", ignoreCase = true) || 
                                                               existingName.contains("TestUnit", ignoreCase = true) ||
                                                               existingName.matches(Regex("[A-Fa-f0-9]{8,}"))
                                        val isSenderAlias = !senderName.contains("MeshCore", ignoreCase = true) &&
                                                          !senderName.contains("TestUnit", ignoreCase = true) &&
                                                          !senderName.matches(Regex("[A-Fa-f0-9]{8,}"))
                                        
                                        // If existing is device ID and sender is alias, consider it a match
                                        val shouldUpdate = isExistingDeviceId && isSenderAlias
                                        
                                        // Also try basic partial matches
                                        val partialMatch = existingName.contains(senderName, ignoreCase = true) ||
                                                         senderName.contains(existingName, ignoreCase = true)
                                        
                                        if (shouldUpdate) {
                                            Timber.i("🔄 Device ID '$existingName' should be updated to alias '$senderName'")
                                        }
                                        
                                        shouldUpdate || partialMatch
                                    } else {
                                        false
                                    }
                                }
                                
                                if (contact != null) {
                                    // Update existing contact with alias name
                                    try {
                                        Timber.i("🔄 Updating existing contact '${contact.name}' to alias: '$senderName'")
                                        val updatedContact = contact.copy(
                                            name = senderName,
                                            lastSeen = System.currentTimeMillis()
                                        )
                                        contactRepository.getNodeDao().updateNode(updatedContact)
                                        Timber.i("✓ Updated existing contact with alias name: '$senderName'")
                                    } catch (e: Exception) {
                                        Timber.e(e, "❌ Failed to update contact with alias name: '$senderName'")
                                    }
                                }
                            }
                            
                            if (contact == null) {
                                // Create new contact from message
                                try {
                                    Timber.i("🆕 Creating new contact from message: '$senderName'")
                                    val newContact = com.meshcore.team.data.database.NodeEntity(
                                        publicKey = ByteArray(32) { 0 }, // Unknown - will be updated when we get sync
                                        hash = 0, // Temporary hash - will be updated when we get sync
                                        name = senderName,
                                        latitude = null,
                                        longitude = null,
                                        batteryMilliVolts = null,
                                        lastSeen = System.currentTimeMillis(),
                                        hopCount = channelMessage.pathLen,
                                        isDirect = channelMessage.pathLen == 0,
                                        isRepeater = false,
                                        isRoomServer = false
                                    )
                                    contactRepository.getNodeDao().insertNode(newContact)
                                    Timber.i("✓ Created new contact from message: '$senderName' with hops=${channelMessage.pathLen}")
                                } catch (e: Exception) {
                                    Timber.e(e, "❌ Failed to create new contact: '$senderName'")
                                }
                            } else {
                                // Update existing contact's lastSeen
                                try {
                                    Timber.i("🔄 Updating lastSeen for existing contact: '${contact.name}'")
                                    val updatedContact = contact.copy(lastSeen = System.currentTimeMillis())
                                    contactRepository.getNodeDao().updateNode(updatedContact)
                                    Timber.i("✓ Updated contact lastSeen: '${contact.name}'")
                                } catch (e: Exception) {
                                    Timber.e(e, "❌ Failed to update contact lastSeen: '${contact.name}'")
                                }
                            }
                        } catch (e: Exception) {
                            Timber.e(e, "❌ Error in contact processing for message from '$senderName'")
                        }
                    }
                    
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
        
        // Listen for incoming direct messages from BLE
        viewModelScope.launch {
            bleConnectionManager.directMessages.collect { directMessage ->
                Timber.i("Processing incoming direct message: text='${directMessage.text}'")
                
                // Look up sender contact by public key
                val sender = contactRepository.getContactByPublicKey(directMessage.senderPubKey)
                val senderName = sender?.name ?: "Unknown"
                
                // Create a synthetic hash for direct messages from this sender
                val directHash = directMessage.senderPubKey[0]
                
                // Save message to database
                val messageEntity = MessageEntity(
                    id = java.util.UUID.randomUUID().toString(),
                    channelHash = directHash, // Use sender's first byte as channel hash for direct messages
                    senderId = directMessage.senderPubKey,
                    senderName = senderName,
                    content = directMessage.text,
                    timestamp = directMessage.timestamp * 1000,
                    isPrivate = true,
                    ackChecksum = null,
                    deliveryStatus = "RECEIVED",
                    heardByCount = 0,
                    attempt = 0,
                    isSentByMe = false
                )
                
                messageRepository.insertMessage(messageEntity)
                Timber.i("✓ Incoming direct message saved from '$senderName': content='${messageEntity.content}'")
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
     * Force refresh contacts to update any alias names
     */
    fun refreshContacts() {
        viewModelScope.launch {
            try {
                Timber.i("🔄 Manually refreshing contacts to sync aliases...")
                contactRepository.syncContacts()
                Timber.i("✓ Contact refresh initiated")
            } catch (e: Exception) {
                Timber.e(e, "❌ Failed to refresh contacts")
            }
        }
    }
    
    /**
     * Clean up duplicate contacts - remove device ID contacts that have alias equivalents
     */
    fun cleanupDuplicateContacts() {
        viewModelScope.launch {
            try {
                val contacts = contactRepository.getAllContacts().first()
                val toDelete = mutableListOf<com.meshcore.team.data.database.NodeEntity>()
                
                Timber.i("🧹 Checking ${contacts.size} contacts for duplicates...")
                
                contacts.forEach { contact ->
                    val name = contact.name
                    if (name != null && (name.matches(Regex("[A-Fa-f0-9]{8}")) || name.contains("MeshCore", ignoreCase = true))) {
                        // This is a device ID contact, check if there's an alias equivalent
                        val hasAliasEquivalent = contacts.any { other ->
                            other.name != null && 
                            other.name != contact.name &&
                            !other.name!!.matches(Regex("[A-Fa-f0-9]{8}")) &&
                            !other.name!!.contains("MeshCore", ignoreCase = true) &&
                            Math.abs((other.latitude ?: 0.0) - (contact.latitude ?: 0.0)) < 0.001 &&
                            Math.abs((other.longitude ?: 0.0) - (contact.longitude ?: 0.0)) < 0.001
                        }
                        
                        if (hasAliasEquivalent) {
                            Timber.i("🗑️ Found device ID contact '$name' with alias equivalent, marking for deletion")
                            toDelete.add(contact)
                        }
                    }
                }
                
                toDelete.forEach { contact ->
                    contactRepository.getNodeDao().deleteNode(contact)
                    Timber.i("✅ Deleted duplicate device ID contact: '${contact.name}'")
                }
                
                if (toDelete.isNotEmpty()) {
                    Timber.i("🧹 Cleaned up ${toDelete.size} duplicate contacts")
                }
            } catch (e: Exception) {
                Timber.e(e, "❌ Failed to cleanup duplicate contacts")
            }
        }
    }
    
    /**
     * Update existing contacts that have device ID names with aliases
     */
    fun updateExistingContactsWithAliases() {
        viewModelScope.launch {
            try {
                val contacts = contactRepository.getAllContacts().first()
                Timber.i("🔍 Checking ${contacts.size} existing contacts for device ID names to update with aliases")
                
                contacts.forEach { contact ->
                    val name = contact.name
                    if (name != null) {
                        Timber.i("🔍 Contact: '$name' - checking if this looks like a device ID")
                        // Check if this looks like a device ID (contains 'MeshCore' or matches device ID pattern)
                        val isDeviceId = name.contains("MeshCore", ignoreCase = true) || 
                                       name.contains("TestUnit", ignoreCase = true) ||
                                       name.matches(Regex("[A-Fa-f0-9]{8,}")) // Hex pattern
                        
                        if (isDeviceId) {
                            Timber.i("⚠️ Found device ID name '$name' - this should be updated with alias when next message is received")
                        }
                    }
                }
            } catch (e: Exception) {
                Timber.e(e, "❌ Failed to check existing contacts")
            }
        }
    }
    
    /**
     * Select a channel
     */
    fun selectChannel(channel: ChannelEntity) {
        Timber.i("🔍 selectChannel called: ${channel.name}")
        Timber.i("🔍 Setting _selectedChannel.value to: ${channel.name}")
        _selectedChannel.value = channel
        Timber.i("🔍 Setting _messagingMode.value to: channels")
        _messagingMode.value = "channels"
        Timber.i("🔍 selectChannel complete: selectedChannel=${_selectedChannel.value?.name}, mode=${_messagingMode.value}")
        Timber.d("Selected channel: ${channel.name}")
    }
    
    /**
     * Select a contact for direct messaging
     */
    fun selectContact(contact: com.meshcore.team.data.database.NodeEntity) {
        _selectedContact.value = contact
        _messagingMode.value = "contacts"
        Timber.d("Selected contact: ${contact.name}")
    }
    
    /**
     * Switch messaging mode
     */
    fun setMessagingMode(mode: String) {
        _messagingMode.value = mode
        Timber.d("Messaging mode: $mode")
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
        Timber.i("🔍 importChannel called: name='$name', psk='${pskBase64.substring(0, minOf(20, pskBase64.length))}...'")
        viewModelScope.launch {
            Timber.i("🔍 importChannel coroutine started")
            val channel = channelRepository.importChannel(name, pskBase64)
            if (channel != null) {
                Timber.i("🔍 Channel imported: ${channel.name}, hash=${channel.hash}")
                
                // Always select the channel first, regardless of firmware sync status
                Timber.i("🔍 About to selectChannel")
                Timber.i("🔍 Current selectedChannel BEFORE: ${_selectedChannel.value?.name}")
                Timber.i("🔍 Current messagingMode BEFORE: ${_messagingMode.value}")
                withContext(Dispatchers.Main) {
                    Timber.i("🔍 Calling selectChannel on Main thread")
                    selectChannel(channel)
                    Timber.i("🔍 selectChannel returned")
                    Timber.i("🔍 Current selectedChannel AFTER: ${_selectedChannel.value?.name}")
                    Timber.i("🔍 Current messagingMode AFTER: ${_messagingMode.value}")
                }
                
                // Try to register with companion radio firmware (optional - will fail if not connected)
                val registered = channelRepository.registerChannelWithFirmware(channel)
                if (registered) {
                    Timber.i("✓ Channel registered with firmware: ${channel.name}")
                } else {
                    Timber.w("⚠️ Channel not registered with firmware (BLE disconnected?) - will sync when connected")
                }
                
                Timber.i("🔍 About to call onResult(true)")
                onResult(true)
                Timber.i("🔍 onResult(true) called")
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
     * Delete a channel from database
     */
    fun deleteChannel(channel: ChannelEntity) {
        viewModelScope.launch {
            channelRepository.deleteChannel(channel)
            Timber.i("Deleted channel: ${channel.name}")
            
            // If deleted channel was selected, switch to first available channel
            if (_selectedChannel.value == channel) {
                channelRepository.getAllChannels().first().firstOrNull()?.let {
                    _selectedChannel.value = it
                }
            }
        }
    }
    
    /**
     * Set device name on companion radio
     */
    fun setDeviceName(name: String) {
        viewModelScope.launch {
            val command = CommandSerializer.setAdvertName(name)
            val success = bleConnectionManager.sendData(command)
            if (success) {
                Timber.i("✓ Device name set to: $name")
            } else {
                Timber.e("✗ Failed to set device name")
            }
        }
    }
    
    /**
     * Send a message (channel or direct)
     */
    fun sendMessage(content: String) {
        if (content.isBlank()) {
            Timber.w("Cannot send empty message")
            return
        }
        
        viewModelScope.launch {
            // Use user alias for display, fallback to "Me"
            val userAlias = appPreferences.userAlias.first() ?: "Me"
            
            // For now, use mock sender data
            // In real implementation, this would come from device identity
            val mockSenderId = ByteArray(32) { it.toByte() }
            
            val mode = _messagingMode.value
            
            if (mode == "contacts") {
                // Send direct message
                val contact = _selectedContact.value ?: return@launch
                
                val message = messageRepository.createOutgoingMessage(
                    senderId = mockSenderId,
                    senderName = userAlias,
                    channelHash = contact.hash, // Use contact hash for direct messages
                    content = content,
                    isPrivate = true
                )
                
                Timber.i("Direct message created: ${message.id}")
                
                // Send via BLE if connected
                if (bleConnectionManager.connectionState.value == ConnectionState.CONNECTED) {
                    try {
                        val timestamp = (System.currentTimeMillis() / 1000).toInt()
                        
                        // Calculate ACK checksum for tracking
                        val ackChecksum = com.meshcore.team.data.ble.AckChecksumCalculator.calculateChecksum(
                            timestamp = timestamp,
                            attempt = 0,
                            text = content,
                            senderPubKey = mockSenderId
                        )
                        
                        Timber.d("Calculated ACK checksum for direct message: ${ackChecksum.joinToString("") { "%02X".format(it) }}")
                        
                        // Store checksum in message
                        val updatedMessage = message.copy(ackChecksum = ackChecksum)
                        messageRepository.updateMessage(updatedMessage)
                        
                        // Prepend alias to message content for mesh-wide visibility
                        val messageWithAlias = "$userAlias: $content"
                        
                        val command = CommandSerializer.sendTextMessage(contact.publicKey, timestamp, messageWithAlias)
                        
                        Timber.d("Attempting to send ${command.size} byte direct message via BLE")
                        val success = bleConnectionManager.sendData(command)
                        if (success) {
                            Timber.i("✓ Direct message with alias written to BLE: ${messageWithAlias.take(30)}...")
                            pendingSentConfirmations[message.id] = message.id
                        } else {
                            Timber.e("✗ BLE sendData returned false for direct message")
                        }
                    } catch (e: Exception) {
                        Timber.e(e, "✗ Exception sending direct message via BLE")
                    }
                }
            } else {
                // Send channel message
                val channel = _selectedChannel.value ?: return@launch
                
                val message = messageRepository.createOutgoingMessage(
                    senderId = mockSenderId,
                    senderName = userAlias,
                    channelHash = channel.hash,
                    content = content,
                    isPrivate = !channel.isPublic
                )
                
                Timber.i("Channel message created: ${message.id}")
                
                // Send via BLE if connected
                if (bleConnectionManager.connectionState.value == ConnectionState.CONNECTED) {
                    try {
                        val timestamp = (System.currentTimeMillis() / 1000).toInt()
                        
                        // Calculate ACK checksum for tracking
                        val ackChecksum = com.meshcore.team.data.ble.AckChecksumCalculator.calculateChecksum(
                            timestamp = timestamp,
                            attempt = 0,
                            text = content,
                            senderPubKey = mockSenderId
                        )
                        
                        Timber.d("Calculated ACK checksum: ${ackChecksum.joinToString("") { "%02X".format(it) }}")
                        
                        // Store checksum in message
                        val updatedMessage = message.copy(ackChecksum = ackChecksum)
                        messageRepository.updateMessage(updatedMessage)
                        
                        // Prepend alias to message content for mesh-wide visibility
                        val messageWithAlias = "$userAlias: $content"
                        
                        val command = CommandSerializer.sendChannelTextMessage(channel.channelIndex, timestamp, messageWithAlias)
                        
                        Timber.d("Attempting to send ${command.size} byte command via BLE")
                        val success = bleConnectionManager.sendData(command)
                        if (success) {
                            Timber.i("✓ Channel message with alias written to BLE: ${messageWithAlias.take(30)}...")
                            pendingSentConfirmations[message.id] = message.id
                        } else {
                            Timber.e("✗ BLE sendData returned false")
                        }
                    } catch (e: Exception) {
                        Timber.e(e, "✗ Exception sending channel message via BLE")
                    }
                } else {
                    Timber.w("Not connected to device - channel message queued locally")
                    messageRepository.updateDeliveryStatus(
                        message.id,
                        com.meshcore.team.data.database.DeliveryStatus.SENT,
                        0
                    )
                }
            }
        }
    }
    
    /* When enabled on a private channel, automatically enables telemetry
     */
    fun toggleLocationSharing(channel: ChannelEntity) {
        viewModelScope.launch {
            val newValue = !channel.shareLocation
            
            // Update channel setting
            channelRepository.updateLocationSharing(channel.hash, newValue)
            Timber.i("📍 Location sharing toggled for ${channel.name}: $newValue")
            
            // If enabling location on a private channel, auto-enable telemetry system
            if (newValue && !channel.isPublic) {
                Timber.i("📍 Enabling telemetry for private channel ${channel.name}")
                appPreferences.setTelemetryEnabled(true)
                appPreferences.setTelemetryChannelHash(channel.hash.toString())
                Timber.i("📍 Telemetry system activated: enabled=true, channel=${channel.name}")
            }
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
    
    /**
     * Handle incoming telemetry message
     * Updates contact location and connectivity without showing in chat
     */
    private suspend fun handleTelemetryMessage(senderName: String, content: String, pathLen: Int) {
        val telemetry = com.meshcore.team.data.model.TelemetryMessage.parse(content)
        if (telemetry == null) {
            Timber.w("Failed to parse telemetry message")
            return
        }
        
        // Find or create contact for this sender
        val contacts = contactRepository.getAllContacts().first()
        
        Timber.i("🔍 All contacts before telemetry processing: ${contacts.map { "'${it.name}'" }.joinToString(", ")}")
        
        // Try exact name match first
        var contact = contacts.firstOrNull { it.name == senderName }
        
        // If no exact match, try variations to handle naming inconsistencies
        if (contact == null) {
            Timber.d("No exact contact match for '$senderName', trying variations...")
            
            // Try to find a contact with a device ID that should be updated to this alias
            contact = contacts.firstOrNull { existingContact ->
                val existingName = existingContact.name
                if (existingName != null) {
                    // Check if existing contact has device ID pattern and sender is a user alias
                    val isExistingDeviceId = existingName.matches(Regex("[A-Fa-f0-9]{8}")) || // 8-char hex
                                           existingName.contains("MeshCore", ignoreCase = true) ||
                                           existingName.contains("TestUnit", ignoreCase = true)
                    
                    val isSenderUserAlias = !senderName.matches(Regex("[A-Fa-f0-9]{8}")) &&
                                          !senderName.contains("MeshCore", ignoreCase = true) &&
                                          !senderName.contains("TestUnit", ignoreCase = true) &&
                                          senderName.length < 20 // Reasonable alias length
                    
                    if (isExistingDeviceId && isSenderUserAlias) {
                        Timber.i("🔄 Found device ID contact '$existingName' that should be updated to alias '$senderName'")
                        return@firstOrNull true
                    }
                    
                    // Also try partial matches
                    existingName.contains(senderName, ignoreCase = true) ||
                    senderName.contains(existingName, ignoreCase = true)
                } else {
                    false
                }
            }
            
            if (contact != null) {
                Timber.d("Found partial contact match: '${contact.name}' for sender '$senderName'")
                // Check if we're updating a device ID to an alias
                val oldName = contact.name
                val isDeviceIdToAlias = oldName != null && 
                                      (oldName.matches(Regex("[A-Fa-f0-9]{8}")) || oldName.contains("MeshCore", ignoreCase = true)) &&
                                      !senderName.matches(Regex("[A-Fa-f0-9]{8}")) &&
                                      !senderName.contains("MeshCore", ignoreCase = true)
                
                // Update contact with the alias name we received
                val updatedContact = contact.copy(
                    name = senderName,
                    latitude = telemetry.latitude,
                    longitude = telemetry.longitude,
                    batteryMilliVolts = telemetry.batteryMilliVolts,
                    lastSeen = System.currentTimeMillis(),
                    hopCount = pathLen,
                    isDirect = pathLen == 0
                )
                contactRepository.getNodeDao().updateNode(updatedContact)
                
                if (isDeviceIdToAlias) {
                    Timber.i("✓ Updated device ID contact '$oldName' to alias: '$senderName'")
                } else {
                    Timber.i("Updated contact name to alias: '$senderName'")
                }
            }
        }
        
        if (contact == null) {
            // Create new contact from telemetry
            Timber.i("📍 Creating new contact from telemetry: $senderName (lat=${telemetry.latitude}, lon=${telemetry.longitude})")
            val newContact = com.meshcore.team.data.database.NodeEntity(
                publicKey = ByteArray(32), // Unknown - will be updated when we get contact sync
                hash = 0, // Temporary hash - will be updated when we get contact sync
                name = senderName,
                latitude = telemetry.latitude,
                longitude = telemetry.longitude,
                batteryMilliVolts = telemetry.batteryMilliVolts,
                lastSeen = System.currentTimeMillis(),
                hopCount = pathLen,
                isDirect = pathLen == 0,
                isRepeater = false,
                isRoomServer = false
            )
            contactRepository.getNodeDao().insertNode(newContact)
            Timber.i("✓ Created contact '$senderName': lat=${telemetry.latitude}, lon=${telemetry.longitude}, hops=$pathLen")
        } else {
            // Update existing contact with new location and connectivity
            val updated = contact.copy(
                latitude = telemetry.latitude ?: contact.latitude,
                longitude = telemetry.longitude ?: contact.longitude,
                batteryMilliVolts = telemetry.batteryMilliVolts ?: contact.batteryMilliVolts,
                lastSeen = System.currentTimeMillis(),
                hopCount = pathLen,
                isDirect = pathLen == 0
            )
            contactRepository.getNodeDao().updateNode(updated)
            Timber.i("✓ Updated contact '$senderName': lat=${telemetry.latitude}, lon=${telemetry.longitude}, hops=$pathLen")
        }
    }
    
    /**
     * Send telemetry message with current location
     */
    fun sendTelemetryMessage(latitude: Double?, longitude: Double?, batteryMilliVolts: Int?) {
        val channel = _selectedChannel.value
        if (channel == null || channel.isPublic) {
            Timber.w("Cannot send telemetry on public channel or no channel selected")
            return
        }
        
        val telemetryText = com.meshcore.team.data.model.TelemetryMessage.create(
            latitude = latitude,
            longitude = longitude,
            batteryMilliVolts = batteryMilliVolts
        )
        
        Timber.i("Sending telemetry message: $telemetryText")
        sendMessage(telemetryText)
    }
    
    /**
     * Clear cached data and resync (for troubleshooting connection issues)
     */
    fun clearDatabaseAndResync() {
        viewModelScope.launch {
            Timber.i("🔄 Clearing cached data and resyncing...")
            
            // Clear all cached data
            messageRepository.deleteAllMessages()
            contactRepository.getNodeDao().deleteAllNodes()
            
            Timber.i("✓ Cache cleared. Resyncing with device...")
            
            // Wait a moment then resync everything
            kotlinx.coroutines.delay(1000)
            if (bleConnectionManager.connectionState.value == ConnectionState.CONNECTED) {
                // Get fresh device info
                val appStartFrame = CommandSerializer.appStart()
                bleConnectionManager.sendData(appStartFrame)
                
                // Sync contacts and channels
                kotlinx.coroutines.delay(500)
                contactRepository.syncContacts()
                
                kotlinx.coroutines.delay(500)
                channelRepository.syncAllChannelsWithFirmware()
                
                Timber.i("✅ Resync completed")
            } else {
                Timber.w("Device not connected - cannot resync")
            }
        }
    }
}
