package com.meshcore.team.data.preferences

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

private val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "app_preferences")

/**
 * Application preferences using DataStore
 */
class AppPreferences(private val context: Context) {
    
    companion object {
        private val LOCATION_SOURCE = stringPreferencesKey("location_source")
        private val TELEMETRY_ENABLED = booleanPreferencesKey("telemetry_enabled")
        private val TELEMETRY_CHANNEL_HASH = stringPreferencesKey("telemetry_channel_hash")
        private val USER_ALIAS = stringPreferencesKey("user_alias")
        
        const val LOCATION_SOURCE_PHONE = "phone"
        const val LOCATION_SOURCE_COMPANION = "companion"
    }
    
    /**
     * Location source preference: "phone" or "companion"
     */
    val locationSource: Flow<String> = context.dataStore.data.map { preferences ->
        preferences[LOCATION_SOURCE] ?: LOCATION_SOURCE_PHONE
    }
    
    suspend fun setLocationSource(source: String) {
        context.dataStore.edit { preferences ->
            preferences[LOCATION_SOURCE] = source
        }
    }
    
    /**
     * Telemetry enabled state
     */
    val telemetryEnabled: Flow<Boolean> = context.dataStore.data.map { preferences ->
        preferences[TELEMETRY_ENABLED] ?: false
    }
    
    suspend fun setTelemetryEnabled(enabled: Boolean) {
        context.dataStore.edit { preferences ->
            preferences[TELEMETRY_ENABLED] = enabled
        }
    }
    
    /**
     * Selected telemetry channel hash
     */
    val telemetryChannelHash: Flow<String?> = context.dataStore.data.map { preferences ->
        preferences[TELEMETRY_CHANNEL_HASH]
    }
    
    suspend fun setTelemetryChannelHash(hash: String?) {
        context.dataStore.edit { preferences ->
            if (hash != null) {
                preferences[TELEMETRY_CHANNEL_HASH] = hash
            } else {
                preferences.remove(TELEMETRY_CHANNEL_HASH)
            }
        }
    }
    
    /**
     * User alias for messages and telemetry (display name)
     */
    val userAlias: Flow<String?> = context.dataStore.data.map { preferences ->
        preferences[USER_ALIAS]
    }
    
    suspend fun setUserAlias(alias: String?) {
        context.dataStore.edit { preferences ->
            if (alias?.isNotBlank() == true) {
                preferences[USER_ALIAS] = alias.trim()
            } else {
                preferences.remove(USER_ALIAS)
            }
        }
    }
}
