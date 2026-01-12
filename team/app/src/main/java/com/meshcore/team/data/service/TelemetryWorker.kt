package com.meshcore.team.data.service

import android.content.Context
import androidx.work.*
import com.meshcore.team.data.database.AppDatabase
import com.meshcore.team.data.ble.BleConnectionManager
import com.meshcore.team.data.ble.ConnectionState
import com.meshcore.team.data.model.TelemetryMessage
import com.meshcore.team.data.preferences.AppPreferences
import com.meshcore.team.data.repository.ChannelRepository
import com.meshcore.team.data.ble.CommandSerializer
import kotlinx.coroutines.flow.first
import timber.log.Timber
import java.util.concurrent.TimeUnit

/**
 * Background worker that periodically sends telemetry messages
 * with location and battery status to private channels
 */
class TelemetryWorker(
    context: Context,
    params: WorkerParameters
) : CoroutineWorker(context, params) {
    
    override suspend fun doWork(): Result {
        Timber.i("📍 TelemetryWorker running...")
        
        try {
            val database = AppDatabase.getDatabase(applicationContext)
            val bleManager = BleConnectionManager.getInstance(applicationContext)
            
            // Check if BLE is connected
            if (bleManager.connectionState.value != ConnectionState.CONNECTED) {
                Timber.w("📍 BLE not connected, skipping telemetry")
                return Result.success()
            }
            Timber.i("📍 BLE connected, proceeding with telemetry")
            
            // Get active private channels
            val channelRepo = ChannelRepository(database.channelDao(), bleManager)
            val channels = channelRepo.getAllChannels().first()
            val privateChannels = channels.filter { !it.isPublic }
            
            if (privateChannels.isEmpty()) {
                Timber.w("📍 No private channels found, skipping telemetry")
                return Result.success()
            }
            Timber.i("📍 Found ${privateChannels.size} private channels")
            
            // Get telemetry channel from preferences
            val appPreferences = AppPreferences(applicationContext)
            val selectedChannelHash = appPreferences.telemetryChannelHash.first()
            
            // Find the selected channel or use first private channel as fallback
            val telemetryChannel = selectedChannelHash?.let { hash ->
                privateChannels.find { it.hash.toString() == hash }
            } ?: privateChannels.first()
            
            Timber.i("📍 Using telemetry channel: ${telemetryChannel.name} (hash=${telemetryChannel.hash})")
            
            // Get location and battery (placeholder values - you'll need to get these from Android APIs)
            val latitude = inputData.getDouble("latitude", 0.0).takeIf { it != 0.0 }
            val longitude = inputData.getDouble("longitude", 0.0).takeIf { it != 0.0 }
            val batteryMv = inputData.getInt("battery_mv", 0).takeIf { it != 0 }
            
            Timber.i("📍 Location data: lat=$latitude, lon=$longitude, battery=$batteryMv")
            
            // Create telemetry message
            val telemetryText = TelemetryMessage.create(
                latitude = latitude,
                longitude = longitude,
                batteryMilliVolts = batteryMv
            )
            
            // Get user alias and embed it in the telemetry message
            val userAlias = appPreferences.userAlias.first()
            val messageWithAlias = if (!userAlias.isNullOrBlank()) {
                "$userAlias: $telemetryText"  // Embed alias: "tomtest: #TEL:{...}"
            } else {
                telemetryText  // No alias, send as-is
            }
            Timber.i("📍 Telemetry with alias: '$messageWithAlias'")
            
            // Send to channel
            val timestamp = (System.currentTimeMillis() / 1000).toInt()
            val command = CommandSerializer.sendChannelTextMessage(
                telemetryChannel.channelIndex,
                timestamp,
                messageWithAlias  // Use alias-embedded message
            )
            
            val success = bleManager.sendData(command)
            
            if (success) {
                Timber.i("✓ Telemetry sent to channel '${telemetryChannel.name}'")
                return Result.success()
            } else {
                Timber.w("✗ Failed to send telemetry")
                return Result.retry()
            }
            
        } catch (e: Exception) {
            Timber.e(e, "Error in TelemetryWorker")
            return Result.failure()
        }
    }
    
    companion object {
        private const val WORK_NAME = "telemetry_periodic"
        
        /**
         * Schedule periodic telemetry sending
         * @param context Application context
         * @param intervalMinutes Interval between telemetry messages (default 1 minute)
         */
        fun schedule(context: Context, intervalMinutes: Long = 1) {
            val constraints = Constraints.Builder()
                .setRequiresBatteryNotLow(false) // Allow on low battery
                .build()
            
            val telemetryRequest = PeriodicWorkRequestBuilder<TelemetryWorker>(
                intervalMinutes, TimeUnit.MINUTES
            )
                .setConstraints(constraints)
                .setInitialDelay(30, TimeUnit.SECONDS) // First run after 30 seconds
                .build()
            
            WorkManager.getInstance(context).enqueueUniquePeriodicWork(
                WORK_NAME,
                ExistingPeriodicWorkPolicy.KEEP,
                telemetryRequest
            )
            
            Timber.i("Telemetry worker scheduled: every $intervalMinutes minute(s)")
        }
        
        /**
         * Cancel telemetry sending
         */
        fun cancel(context: Context) {
            WorkManager.getInstance(context).cancelUniqueWork(WORK_NAME)
            Timber.i("Telemetry worker cancelled")
        }
        
        /**
         * Update location data for next telemetry message
         */
        fun updateLocation(context: Context, latitude: Double, longitude: Double, batteryMv: Int? = null) {
            val data = Data.Builder()
                .putDouble("latitude", latitude)
                .putDouble("longitude", longitude)
                .apply {
                    batteryMv?.let { putInt("battery_mv", it) }
                }
                .build()
            
            // Trigger one-time send with updated location
            val oneTimeRequest = OneTimeWorkRequestBuilder<TelemetryWorker>()
                .setInputData(data)
                .build()
            
            WorkManager.getInstance(context).enqueue(oneTimeRequest)
        }
    }
}
