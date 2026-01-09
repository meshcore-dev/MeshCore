package com.meshcore.team

import android.app.Application
import timber.log.Timber

/**
 * TEAM Application class
 * Initializes logging and other app-wide components
 */
class TeamApplication : Application() {
    
    override fun onCreate() {
        super.onCreate()
        
        // Initialize Timber logging
        if (BuildConfig.DEBUG) {
            Timber.plant(Timber.DebugTree())
        }
        
        Timber.i("TEAM Application started")
    }
}
