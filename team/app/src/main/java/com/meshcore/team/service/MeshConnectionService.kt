package com.meshcore.team.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import androidx.core.app.NotificationCompat
import com.meshcore.team.MainActivity
import com.meshcore.team.R
import com.meshcore.team.data.ble.BleConnectionManager
import com.meshcore.team.data.ble.ConnectionState
import com.meshcore.team.data.preferences.AppPreferences
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.first
import timber.log.Timber

/**
 * Foreground service that maintains BLE connection and mesh operations in background
 */
class MeshConnectionService : Service() {
    
    private val serviceScope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private lateinit var bleConnectionManager: BleConnectionManager
    private lateinit var appPreferences: AppPreferences
    private var wakeLock: PowerManager.WakeLock? = null
    
    companion object {
        const val CHANNEL_ID = "mesh_connection_channel"
        const val NOTIFICATION_ID = 1001
        const val ACTION_START_SERVICE = "START_MESH_SERVICE"
        const val ACTION_STOP_SERVICE = "STOP_MESH_SERVICE"
        
        fun startService(context: Context) {
            val intent = Intent(context, MeshConnectionService::class.java).apply {
                action = ACTION_START_SERVICE
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }
        
        fun stopService(context: Context) {
            val intent = Intent(context, MeshConnectionService::class.java).apply {
                action = ACTION_STOP_SERVICE
            }
            context.startService(intent)
        }
    }
    
    override fun onCreate() {
        super.onCreate()
        Timber.i("🔄 MeshConnectionService created")
        
        bleConnectionManager = BleConnectionManager.getInstance(this)
        appPreferences = AppPreferences(this)
        
        createNotificationChannel()
        acquireWakeLock()
    }
    
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        Timber.i("🔄 MeshConnectionService starting with action: ${intent?.action}")
        
        when (intent?.action) {
            ACTION_START_SERVICE -> {
                startForegroundService()
                startBackgroundOperations()
            }
            ACTION_STOP_SERVICE -> {
                stopSelf()
            }
        }
        
        // Restart service if killed by system
        return START_STICKY
    }
    
    override fun onDestroy() {
        super.onDestroy()
        Timber.i("🔄 MeshConnectionService destroyed")
        
        serviceScope.cancel()
        releaseWakeLock()
    }
    
    override fun onBind(intent: Intent?): IBinder? = null
    
    private fun startForegroundService() {
        val notification = createNotification("Mesh network active", "Connected to companion device")
        startForeground(NOTIFICATION_ID, notification)
        Timber.i("🔄 Foreground service started")
    }
    
    private fun startBackgroundOperations() {
        serviceScope.launch {
            // Monitor BLE connection state and update notification
            bleConnectionManager.connectionState.collect { state ->
                val notification = when (state) {
                    ConnectionState.CONNECTED -> {
                        val deviceName = bleConnectionManager.connectedDevice.first()?.name ?: "Unknown"
                        createNotification("Mesh network connected", "Connected to $deviceName")
                    }
                    ConnectionState.CONNECTING -> {
                        createNotification("Mesh network connecting", "Establishing connection...")
                    }
                    ConnectionState.DISCONNECTING -> {
                        createNotification("Mesh network disconnecting", "Closing connection...")
                    }
                    ConnectionState.DISCONNECTED -> {
                        createNotification("Mesh network disconnected", "Searching for device...")
                    }
                    ConnectionState.ERROR -> {
                        createNotification("Mesh network error", "Connection error occurred")
                    }
                }
                
                val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
                notificationManager.notify(NOTIFICATION_ID, notification)
            }
        }
        
        serviceScope.launch {
            // Keep BLE connection alive
            while (isActive) {
                try {
                    if (bleConnectionManager.connectionState.first() == ConnectionState.DISCONNECTED) {
                        Timber.i("🔄 BLE disconnected in background, attempting reconnection...")
                        // BLE manager will automatically try to reconnect
                    }
                } catch (e: Exception) {
                    Timber.e(e, "🔄 Error in background BLE monitoring")
                }
                delay(30000) // Check every 30 seconds
            }
        }
    }
    
    private fun createNotification(title: String, content: String): Notification {
        val intent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
        }
        val pendingIntent = PendingIntent.getActivity(
            this, 0, intent, 
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(title)
            .setContentText(content)
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
            .build()
    }
    
    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Mesh Network Connection",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Maintains mesh network connection in background"
                setShowBadge(false)
                setSound(null, null)
            }
            
            val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            notificationManager.createNotificationChannel(channel)
        }
    }
    
    private fun acquireWakeLock() {
        try {
            val powerManager = getSystemService(Context.POWER_SERVICE) as PowerManager
            wakeLock = powerManager.newWakeLock(
                PowerManager.PARTIAL_WAKE_LOCK,
                "MeshCore::MeshConnectionService"
            ).apply {
                acquire(10*60*1000L /*10 minutes*/)
                Timber.i("🔄 Wake lock acquired")
            }
        } catch (e: Exception) {
            Timber.e(e, "🔄 Failed to acquire wake lock")
        }
    }
    
    private fun releaseWakeLock() {
        try {
            wakeLock?.let {
                if (it.isHeld) {
                    it.release()
                    Timber.i("🔄 Wake lock released")
                }
            }
        } catch (e: Exception) {
            Timber.e(e, "🔄 Failed to release wake lock")
        }
    }
}