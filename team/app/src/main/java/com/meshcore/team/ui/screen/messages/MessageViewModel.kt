package com.meshcore.team.ui.screen.messages

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.meshcore.team.data.database.ChannelEntity
import com.meshcore.team.data.database.MessageEntity
import com.meshcore.team.data.repository.ChannelRepository
import com.meshcore.team.data.repository.MessageRepository
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch
import timber.log.Timber

/**
 * ViewModel for messaging screen
 */
class MessageViewModel(
    private val messageRepository: MessageRepository,
    private val channelRepository: ChannelRepository
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
    
    init {
        // Initialize default public channel
        viewModelScope.launch {
            channelRepository.initializeDefaultChannel()
            
            // Select first channel (public channel)
            channelRepository.getAllChannels().first().firstOrNull()?.let { firstChannel ->
                _selectedChannel.value = firstChannel
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
            // In real implementation, this would come from device
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
            
            // TODO: Send to device via BLE when connected
            // For now, just mark as sent immediately for testing
            messageRepository.updateDeliveryStatus(
                message.id,
                com.meshcore.team.data.database.DeliveryStatus.SENT,
                0
            )
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
            val mockSenderId = ByteArray(32) { it.toByte() }
            
            // Add some test messages
            listOf(
                "Hey, where are you?",
                "I'm at the north stand",
                "Copy that, heading your way",
                "See anything yet?"
            ).forEachIndexed { index, text ->
                messageRepository.createOutgoingMessage(
                    senderId = mockSenderId,
                    senderName = "TestUser",
                    channelHash = channel.hash,
                    content = text,
                    isPrivate = false
                )
            }
            
            Timber.i("Added mock messages")
        }
    }
}
