package com.meshcore.team.ui.screen.messages

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.google.zxing.integration.android.IntentIntegrator
import com.meshcore.team.data.ble.ConnectionState
import com.meshcore.team.data.database.ChannelEntity
import com.meshcore.team.data.database.DeliveryStatus
import com.meshcore.team.data.database.MessageEntity
import com.meshcore.team.utils.QRCodeGenerator
import java.text.SimpleDateFormat
import java.util.*

/**
 * Main messaging screen with channel selector and message list
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MessageScreen(
    viewModel: MessageViewModel,
    onNavigateToConnection: () -> Unit
) {
    val channels by viewModel.channels.collectAsState()
    val selectedChannel by viewModel.selectedChannel.collectAsState()
    val messages by viewModel.messages.collectAsState()
    val connectionState by viewModel.connectionState.collectAsState()
    val connectedDevice by viewModel.connectedDevice.collectAsState()
    
    var showChannelMenu by remember { mutableStateOf(false) }
    var showCreateChannelDialog by remember { mutableStateOf(false) }
    var showImportChannelDialog by remember { mutableStateOf(false) }
    var showShareChannelDialog by remember { mutableStateOf(false) }
    var messageText by remember { mutableStateOf("") }
    
    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Text(selectedChannel?.name ?: "Messages")
                        
                        // Connection status indicator
                        Box(
                            modifier = Modifier
                                .size(8.dp)
                                .background(
                                    color = when (connectionState) {
                                        ConnectionState.CONNECTED -> MaterialTheme.colorScheme.primary
                                        ConnectionState.CONNECTING -> MaterialTheme.colorScheme.tertiary
                                        else -> MaterialTheme.colorScheme.error
                                    },
                                    shape = CircleShape
                                )
                        )
                        
                        if (connectionState == ConnectionState.CONNECTED) {
                            Text(
                                text = connectedDevice?.name ?: "Connected",
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                },
                actions = {
                    // Share channel key button (for private channels)
                    selectedChannel?.let { channel ->
                        if (!channel.isPublic) {
                            IconButton(onClick = { showShareChannelDialog = true }) {
                                Icon(Icons.Default.Share, "Share Channel Key")
                            }
                        }
                    }
                    
                    // Channel selector
                    IconButton(onClick = { showChannelMenu = true }) {
                        Icon(Icons.Default.Menu, "Select Channel")
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            // Channel info card
            selectedChannel?.let { channel ->
                ChannelInfoCard(
                    channel = channel,
                    onToggleLocation = { viewModel.toggleLocationSharing(channel) }
                )
            }
            
            // Messages list
            MessageList(
                messages = messages,
                modifier = Modifier.weight(1f)
            )
            
            // Message input
            MessageInput(
                text = messageText,
                onTextChange = { messageText = it },
                onSend = {
                    viewModel.sendMessage(messageText)
                    messageText = ""
                }
            )
        }
        
        // Channel selection menu
        if (showChannelMenu) {
            ChannelSelectionDialog(
                channels = channels,
                selectedChannel = selectedChannel,
                onChannelSelected = { 
                    viewModel.selectChannel(it)
                    showChannelMenu = false
                },
                onCreateChannel = {
                    showChannelMenu = false
                    showCreateChannelDialog = true
                },
                onDismiss = { showChannelMenu = false }
            )
        }
        
        // Create private channel dialog
        if (showCreateChannelDialog) {
            CreateChannelDialog(
                onConfirm = { name ->
                    viewModel.createPrivateChannel(name)
                    showCreateChannelDialog = false
                },
                onDismiss = { showCreateChannelDialog = false }
            )
        }
        
        // Import channel dialog
        if (showImportChannelDialog) {
            ImportChannelDialog(
                onConfirm = { nameOrUrl, keyOrEmpty ->
                    // Check if first param is a meshcore:// URL
                    val input = if (nameOrUrl.startsWith("meshcore://")) {
                        nameOrUrl // URL in first field
                    } else if (keyOrEmpty.startsWith("meshcore://")) {
                        keyOrEmpty // URL in second field
                    } else {
                        nameOrUrl // Assume legacy format with separate name/key
                    }
                    viewModel.importChannel(input, keyOrEmpty) { success ->
                        if (!success) {
                            // TODO: Show error toast
                        }
                    }
                    showImportChannelDialog = false
                },
                onDismiss = { showImportChannelDialog = false }
            )
        }
        
        // 
        // Share channel key dialog
        if (showShareChannelDialog && selectedChannel != null) {
            ShareChannelDialog(
                channel = selectedChannel!!,
                shareKey = viewModel.getChannelShareKey(selectedChannel!!),
                onDismiss = { showShareChannelDialog = false }
            )
        }
    }
}

@Composable
fun ChannelInfoCard(
    channel: ChannelEntity,
    onToggleLocation: () -> Unit
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(8.dp),
        colors = CardDefaults.cardColors(
            containerColor = if (channel.isPublic) 
                MaterialTheme.colorScheme.surfaceVariant 
            else 
                MaterialTheme.colorScheme.primaryContainer
        )
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = if (channel.isPublic) "Public Channel" else "Private Channel",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(
                        imageVector = if (channel.shareLocation) Icons.Default.LocationOn else Icons.Default.LocationOff,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp),
                        tint = if (channel.shareLocation) 
                            MaterialTheme.colorScheme.primary 
                        else 
                            MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(modifier = Modifier.width(4.dp))
                    Text(
                        text = if (channel.shareLocation) "Sharing Location" else "Location OFF",
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }
            
            if (channel.isPublic) {
                Switch(
                    checked = channel.shareLocation,
                    onCheckedChange = { onToggleLocation() }
                )
            } else {
                Icon(
                    imageVector = Icons.Default.Lock,
                    contentDescription = "Location Required",
                    tint = MaterialTheme.colorScheme.primary
                )
            }
        }
    }
}

@Composable
fun MessageList(
    messages: List<MessageEntity>,
    modifier: Modifier = Modifier
) {
    val listState = rememberLazyListState()
    
    LaunchedEffect(messages.size) {
        if (messages.isNotEmpty()) {
            listState.animateScrollToItem(messages.size - 1)
        }
    }
    
    if (messages.isEmpty()) {
        Box(
            modifier = modifier.fillMaxSize(),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = "No messages yet.\nSend a message to get started!",
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    } else {
        LazyColumn(
            modifier = modifier,
            state = listState,
            contentPadding = PaddingValues(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(
                items = messages,
                key = { message -> message.id }
            ) { message ->
                MessageBubble(message)
            }
        }
    }
}

@Composable
fun MessageBubble(message: MessageEntity) {
    val isSentByMe = message.isSentByMe
    val timeFormat = SimpleDateFormat("HH:mm", Locale.getDefault())
    
    // Force recomposition when deliveryStatus changes
    key(message.id, message.deliveryStatus) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = if (isSentByMe) Arrangement.End else Arrangement.Start
        ) {
            Card(
                modifier = Modifier.widthIn(max = 280.dp),
                shape = RoundedCornerShape(12.dp),
                colors = CardDefaults.cardColors(
                    containerColor = if (isSentByMe)
                        MaterialTheme.colorScheme.primaryContainer
                    else
                        MaterialTheme.colorScheme.surfaceVariant
                )
            ) {
                Column(
                    modifier = Modifier.padding(12.dp)
                ) {
                // Show sender name/tag for messages from others
                if (!isSentByMe) {
                    val senderLabel = message.senderName ?: run {
                        // Generate short tag from senderId (last 4 bytes as hex)
                        val hashBytes = message.senderId.takeLast(4)
                        hashBytes.joinToString("") { "%02X".format(it) }
                    }
                    
                    Text(
                        text = senderLabel,
                        style = MaterialTheme.typography.labelSmall,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.primary
                    )
                    Spacer(modifier = Modifier.height(4.dp))
                }
                
                Text(
                    text = message.content,
                    style = MaterialTheme.typography.bodyMedium
                )
                
                // Debug log for received messages
                if (!isSentByMe) {
                    androidx.compose.runtime.LaunchedEffect(message.id) {
                        timber.log.Timber.d("Displaying received message: id=${message.id}, content='${message.content}', length=${message.content.length}")
                    }
                }
                
                Spacer(modifier = Modifier.height(4.dp))
                
                Row(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = timeFormat.format(Date(message.timestamp)),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    
                    if (isSentByMe) {
                        DeliveryStatusIcon(
                            status = DeliveryStatus.valueOf(message.deliveryStatus),
                            count = message.heardByCount
                        )
                    }
                }
            }
        }
    }
    }
}

@Composable
fun DeliveryStatusIcon(status: DeliveryStatus, count: Int) {
    Row(
        horizontalArrangement = Arrangement.spacedBy(2.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        when (status) {
            DeliveryStatus.SENDING -> {
                Icon(
                    imageVector = Icons.Default.Schedule,
                    contentDescription = "Sending",
                    modifier = Modifier.size(14.dp),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            DeliveryStatus.SENT -> {
                Icon(
                    imageVector = Icons.Default.Check,
                    contentDescription = "Sent",
                    modifier = Modifier.size(14.dp),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            DeliveryStatus.DELIVERED -> {
                Icon(
                    imageVector = Icons.Default.DoneAll,
                    contentDescription = "Delivered",
                    modifier = Modifier.size(14.dp),
                    tint = MaterialTheme.colorScheme.primary
                )
                if (count > 0) {
                    Text(
                        text = "($count)",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.primary,
                        fontWeight = FontWeight.Medium
                    )
                }
            }
        }
    }
}

@Composable
fun MessageInput(
    text: String,
    onTextChange: (String) -> Unit,
    onSend: () -> Unit
) {
    Surface(
        tonalElevation = 3.dp
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            OutlinedTextField(
                value = text,
                onValueChange = onTextChange,
                modifier = Modifier.weight(1f),
                placeholder = { Text("Type a message...") },
                maxLines = 4
            )
            
            IconButton(
                onClick = onSend,
                enabled = text.isNotBlank()
            ) {
                Icon(
                    imageVector = Icons.Default.Send,
                    contentDescription = "Send",
                    tint = if (text.isNotBlank())
                        MaterialTheme.colorScheme.primary
                    else
                        MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
fun ChannelSelectionDialog(
    channels: List<ChannelEntity>,
    selectedChannel: ChannelEntity?,
    onChannelSelected: (ChannelEntity) -> Unit,
    onCreateChannel: () -> Unit,
    onDismiss: () -> Unit
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Select Channel") },
        text = {
            Column {
                channels.forEach { channel ->
                    Card(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 4.dp)
                            .clickable { onChannelSelected(channel) },
                        colors = CardDefaults.cardColors(
                            containerColor = if (channel == selectedChannel)
                                MaterialTheme.colorScheme.primaryContainer
                            else
                                MaterialTheme.colorScheme.surface
                        )
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(16.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column {
                                Text(
                                    text = channel.name,
                                    style = MaterialTheme.typography.titleMedium,
                                    fontWeight = FontWeight.Bold
                                )
                                Text(
                                    text = if (channel.isPublic) "Public" else "Private",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            
                            if (channel == selectedChannel) {
                                Icon(
                                    imageVector = Icons.Default.Check,
                                    contentDescription = "Selected",
                                    tint = MaterialTheme.colorScheme.primary
                                )
                            }
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onCreateChannel) {
                Icon(Icons.Default.Add, contentDescription = null, modifier = Modifier.size(18.dp))
                Spacer(modifier = Modifier.width(4.dp))
                Text("New Channel")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Close")
            }
        }
    )
}

@Composable
fun CreateChannelDialog(
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit
) {
    var channelName by remember { mutableStateOf("") }
    
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Create Private Channel") },
        text = {
            Column {
                Text(
                    text = "Create a private channel with a secure encryption key. You'll be able to share the key with others to invite them.",
                    style = MaterialTheme.typography.bodyMedium
                )
                Spacer(modifier = Modifier.height(16.dp))
                OutlinedTextField(
                    value = channelName,
                    onValueChange = { channelName = it },
                    label = { Text("Channel Name") },
                    placeholder = { Text("e.g., Hunting Team") },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true
                )
            }
        },
        confirmButton = {
            TextButton(
                onClick = { onConfirm(channelName) },
                enabled = channelName.isNotBlank()
            ) {
                Text("Create")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancel")
            }
        }
    )
}

@Composable
fun ShareChannelDialog(
    channel: ChannelEntity,
    shareKey: String,
    onDismiss: () -> Unit
) {
    // Generate QR code
    val qrBitmap = remember(shareKey) {
        QRCodeGenerator.generateQRCode(shareKey, 400)
    }
    
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Share \"${channel.name}\"") },
        text = {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text(
                    text = "Share this link with others to invite them to this private channel:",
                    style = MaterialTheme.typography.bodyMedium
                )
                Spacer(modifier = Modifier.height(16.dp))
                
                // QR Code
                Card(
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surface
                    ),
                    modifier = Modifier.padding(8.dp)
                ) {
                    Image(
                        bitmap = qrBitmap.asImageBitmap(),
                        contentDescription = "Channel Key QR Code",
                        modifier = Modifier
                            .size(250.dp)
                            .padding(16.dp)
                    )
                }
                
                Spacer(modifier = Modifier.height(16.dp))
                
                // Text key
                Card(
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surfaceVariant
                    )
                ) {
                    Text(
                        text = shareKey,
                        modifier = Modifier.padding(12.dp),
                        style = MaterialTheme.typography.bodySmall,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
                Spacer(modifier = Modifier.height(8.dp))
                Text(
                    text = "⚠️ Keep this key secure! Anyone with it can join this channel.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error
                )
            }
        },
        confirmButton = {
            TextButton(onClick = {
                // TODO: Copy to clipboard
                onDismiss()
            }) {
                Icon(Icons.Default.Share, contentDescription = null, modifier = Modifier.size(18.dp))
                Spacer(modifier = Modifier.width(4.dp))
                Text("Copy Key")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Close")
            }
        }
    )
}

@Composable
fun ImportChannelDialog(
    onConfirm: (String, String) -> Unit,
    onDismiss: () -> Unit
) {
    var channelName by remember { mutableStateOf("") }
    var channelKey by remember { mutableStateOf("") }
    var showScanner by remember { mutableStateOf(false) }
    
    if (showScanner) {
        // QR Scanner will be implemented with CameraX
        // For now, show a placeholder
        AlertDialog(
            onDismissRequest = { showScanner = false },
            title = { Text("QR Scanner") },
            text = {
                Text("QR scanner will be implemented here using CameraX.\n\nFor now, please paste the key manually.")
            },
            confirmButton = {
                TextButton(onClick = { showScanner = false }) {
                    Text("OK")
                }
            }
        )
    } else {
        AlertDialog(
            onDismissRequest = onDismiss,
            title = { Text("Import Channel") },
            text = {
                Column {
                    Text(
                        text = "Import a private channel using a shared key:",
                        style = MaterialTheme.typography.bodyMedium
                    )
                    Spacer(modifier = Modifier.height(16.dp))
                    
                    OutlinedTextField(
                        value = channelName,
                        onValueChange = { channelName = it },
                        label = { Text("Channel Name") },
                        modifier = Modifier.fillMaxWidth()
                    )
                    
                    Spacer(modifier = Modifier.height(12.dp))
                    
                    OutlinedTextField(
                        value = channelKey,
                        onValueChange = { channelKey = it },
                        label = { Text("Channel URL or Key") },
                        modifier = Modifier.fillMaxWidth(),
                        maxLines = 4,
                        supportingText = { Text("Paste meshcore:// URL or base64/hex key") }
                    )
                    
                    Spacer(modifier = Modifier.height(8.dp))
                    
                    OutlinedButton(
                        onClick = { showScanner = true },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Default.QrCode, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Scan QR Code")
                    }
                }
            },
            confirmButton = {
                val hasUrl = channelName.startsWith("meshcore://") || channelKey.startsWith("meshcore://")
                val hasLegacyFormat = channelName.isNotBlank() && channelKey.isNotBlank()
                TextButton(
                    onClick = {
                        onConfirm(channelName.trim(), channelKey.trim())
                    },
                    enabled = hasUrl || hasLegacyFormat
                ) {
                    Text("Import")
                }
            },
            dismissButton = {
                TextButton(onClick = onDismiss) {
                    Text("Cancel")
                }
            }
        )
    }
}

