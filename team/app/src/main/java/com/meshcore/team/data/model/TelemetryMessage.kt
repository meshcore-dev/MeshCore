package com.meshcore.team.data.model

import org.json.JSONObject
import timber.log.Timber

/**
 * Telemetry message for position/connectivity tracking
 * Sent as hidden messages in private channels
 * 
 * Format: #TEL:{"lat":12.345,"lon":67.890,"batt":3850,"time":1234567890}
 */
data class TelemetryMessage(
    val latitude: Double?,
    val longitude: Double?,
    val batteryMilliVolts: Int?,
    val timestamp: Long
) {
    
    companion object {
        const val PREFIX = "#TEL:"
        
        /**
         * Check if a message is a telemetry message
         */
        fun isTelemetryMessage(text: String): Boolean {
            return text.startsWith(PREFIX)
        }
        
        /**
         * Parse telemetry message from text
         * Returns null if parsing fails
         */
        fun parse(text: String): TelemetryMessage? {
            if (!isTelemetryMessage(text)) return null
            
            try {
                val jsonString = text.substring(PREFIX.length)
                val json = JSONObject(jsonString)
                
                return TelemetryMessage(
                    latitude = if (json.has("lat")) json.getDouble("lat") else null,
                    longitude = if (json.has("lon")) json.getDouble("lon") else null,
                    batteryMilliVolts = if (json.has("batt")) json.getInt("batt") else null,
                    timestamp = json.getLong("time")
                )
            } catch (e: Exception) {
                Timber.e(e, "Failed to parse telemetry message: $text")
                return null
            }
        }
        
        /**
         * Create telemetry message text
         */
        fun create(
            latitude: Double?,
            longitude: Double?,
            batteryMilliVolts: Int?
        ): String {
            val json = JSONObject()
            latitude?.let { json.put("lat", it) }
            longitude?.let { json.put("lon", it) }
            batteryMilliVolts?.let { json.put("batt", it) }
            json.put("time", System.currentTimeMillis() / 1000) // Unix timestamp in seconds
            
            return "$PREFIX$json"
        }
    }
}
