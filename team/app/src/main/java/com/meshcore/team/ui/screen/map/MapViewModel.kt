package com.meshcore.team.ui.screen.map

import android.annotation.SuppressLint
import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.google.android.gms.location.FusedLocationProviderClient
import com.google.android.gms.location.LocationRequest
import com.google.android.gms.location.LocationServices
import com.google.android.gms.location.Priority
import com.google.android.gms.tasks.CancellationTokenSource
import com.meshcore.team.data.database.NodeEntity
import com.meshcore.team.data.preferences.AppPreferences
import com.meshcore.team.data.repository.ContactRepository
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlinx.coroutines.tasks.await
import timber.log.Timber

/**
 * ViewModel for map screen
 */
class MapViewModel(
    private val context: Context,
    private val contactRepository: ContactRepository,
    private val appPreferences: AppPreferences
) : ViewModel() {
    
    private val fusedLocationClient: FusedLocationProviderClient = 
        LocationServices.getFusedLocationProviderClient(context)
    
    private val _currentLocation = MutableStateFlow<LocationData?>(null)
    val currentLocation: StateFlow<LocationData?> = _currentLocation.asStateFlow()
    
    // Manually refreshable nodes list
    private val _nodes = MutableStateFlow<List<NodeEntity>>(emptyList())
    val nodes: StateFlow<List<NodeEntity>> = _nodes.asStateFlow()
    
    // Location source preference (phone or companion)
    val locationSource: StateFlow<String> = appPreferences.locationSource
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(5000),
            initialValue = AppPreferences.LOCATION_SOURCE_PHONE
        )
    
    init {
        // Initial load and auto-refresh from database
        viewModelScope.launch {
            contactRepository.getAllContacts().collect { nodeList ->
                _nodes.value = nodeList
                Timber.i("📍 Map contacts updated from Flow: ${nodeList.size} total contacts")
                nodeList.forEach { node ->
                    Timber.i("📍 Contact: '${node.name}' - lat=${node.latitude}, lon=${node.longitude}, lastSeen=${node.lastSeen}")
                }
            }
        }
    }
    
    /**
     * Manually refresh nodes from database
     * Call this to force fresh data without waiting for Flow emissions
     * @return Fresh list of nodes directly from database
     */
    suspend fun refreshNodes(): List<NodeEntity> {
        val freshNodes = contactRepository.getAllContactsDirect()
        _nodes.value = freshNodes
        Timber.d("📍 Manual refresh: loaded ${freshNodes.size} nodes from database")
        freshNodes.forEach { node ->
            Timber.d("📍   - '${node.name}': lastSeen=${node.lastSeen} (${System.currentTimeMillis() - node.lastSeen}ms ago)")
        }
        return freshNodes
    }
    
    init {
        // Start location updates
        startLocationUpdates()
    }
    
    /**
     * Start location updates based on selected source
     */
    private fun startLocationUpdates() {
        viewModelScope.launch {
            locationSource.collect { source ->
                when (source) {
                    AppPreferences.LOCATION_SOURCE_PHONE -> {
                        fetchPhoneLocation()
                    }
                    AppPreferences.LOCATION_SOURCE_COMPANION -> {
                        fetchCompanionLocation()
                    }
                }
            }
        }
    }
    
    /**
     * Fetch location from phone GPS
     */
    @SuppressLint("MissingPermission")
    private suspend fun fetchPhoneLocation() {
        try {
            val cancellationTokenSource = CancellationTokenSource()
            val location = fusedLocationClient.getCurrentLocation(
                Priority.PRIORITY_HIGH_ACCURACY,
                cancellationTokenSource.token
            ).await()
            
            location?.let {
                _currentLocation.value = LocationData(
                    latitude = it.latitude,
                    longitude = it.longitude,
                    altitude = if (it.hasAltitude()) it.altitude else null,
                    accuracy = if (it.hasAccuracy()) it.accuracy else null
                )
                Timber.d("Phone GPS: ${it.latitude}, ${it.longitude}")
            }
        } catch (e: Exception) {
            Timber.e(e, "Failed to get phone location")
        }
    }
    
    /**
     * Fetch location from companion radio
     * TODO: Parse GPS data from companion telemetry
     */
    private suspend fun fetchCompanionLocation() {
        // TODO: Get GPS coordinates from companion radio
        // This will depend on how the companion sends GPS data
        // Might be in telemetry responses or a dedicated GPS command
        Timber.d("Companion GPS not yet implemented")
    }
    
    /**
     * Manually refresh location
     */
    fun refreshLocation() {
        viewModelScope.launch {
            when (locationSource.value) {
                AppPreferences.LOCATION_SOURCE_PHONE -> fetchPhoneLocation()
                AppPreferences.LOCATION_SOURCE_COMPANION -> fetchCompanionLocation()
            }
        }
    }
}
