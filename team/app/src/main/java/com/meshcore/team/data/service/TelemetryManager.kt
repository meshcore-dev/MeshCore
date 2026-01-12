package com.meshcore.team.data.service

import android.content.Context
import androidx.work.*
import com.meshcore.team.data.preferences.AppPreferences
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.launch
import timber.log.Timber

/**
 * Manages telemetry sending based on user preferences
 * Sends location updates every 30 seconds when enabled
 */
class TelemetryManager(
    private val context: Context,
    private val appPreferences: AppPreferences,
    private val scope: CoroutineScope
) {
    
    private var telemetryJob: Job? = null
    private var lastLatitude: Double? = null
    private var lastLongitude: Double? = null
    
    init {
        // Monitor telemetry settings and start/stop sending
        scope.launch {
            combine(
                appPreferences.telemetryEnabled,
                appPreferences.telemetryChannelHash
            ) { enabled, channelHash ->
                Pair(enabled, channelHash)
            }.collect { (enabled, channelHash) ->
                Timber.i("📍 Telemetry settings: enabled=$enabled, channelHash=$channelHash")
                if (enabled && channelHash != null) {
                    Timber.i("📍 Starting 30-second telemetry updates")
                    startTelemetry()
                } else {
                    Timber.i("📍 Stopping telemetry updates (enabled=$enabled, channel set=${channelHash != null})")
                    stopTelemetry()
                }
            }
        }
    }
    
    /**
     * Start periodic telemetry sending (every 30 seconds)
     */
    private fun startTelemetry() {
        // Cancel existing job if any
        telemetryJob?.cancel()
        
        telemetryJob = scope.launch {
            while (true) {
                // Send telemetry with last known location
                lastLatitude?.let { lat ->
                    lastLongitude?.let { lon ->
                        sendLocationUpdate(lat, lon)
                        Timber.i("📍 Periodic telemetry sent: lat=$lat, lon=$lon")
                    }
                }
                
                // Wait 30 seconds before next update
                delay(30_000)
            }
        }
        Timber.i("📍 30-second telemetry loop started")
    }
    
    /**
     * Stop telemetry worker
     */
    private fun stopTelemetry() {
        telemetryJob?.cancel()
        telemetryJob = null
        Timber.i("📍 Telemetry job stopped")
    }
    
    /**
     * Send immediate telemetry update with location
     * Also updates the cached location for periodic sends
     */
    fun sendLocationUpdate(latitude: Double, longitude: Double, batteryMv: Int? = null) {
        // Cache location for periodic telemetry
        lastLatitude = latitude
        lastLongitude = longitude
        
        val data = Data.Builder()
            .putDouble("latitude", latitude)
            .putDouble("longitude", longitude)
            .apply {
                batteryMv?.let { putInt("battery_mv", it) }
            }
            .build()
        
        val oneTimeRequest = OneTimeWorkRequestBuilder<TelemetryWorker>()
            .setInputData(data)
            .build()
        
        WorkManager.getInstance(context).enqueue(oneTimeRequest)
        Timber.d("📍 Location update queued: lat=$latitude, lon=$longitude")
    }
}
