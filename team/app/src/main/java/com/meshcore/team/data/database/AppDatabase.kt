package com.meshcore.team.data.database

import android.content.Context
import androidx.room.Database
import androidx.room.Room
import androidx.room.RoomDatabase
import androidx.room.TypeConverters

/**
 * Main Room database for TEAM app
 */
@Database(
    entities = [
        MessageEntity::class,
        NodeEntity::class,
        ChannelEntity::class,
        AckRecordEntity::class
    ],
    version = 2, // Incremented for channelIndex field addition
    exportSchema = false
)
@TypeConverters(Converters::class)
abstract class AppDatabase : RoomDatabase() {
    
    abstract fun messageDao(): MessageDao
    abstract fun nodeDao(): NodeDao
    abstract fun channelDao(): ChannelDao
    abstract fun ackRecordDao(): AckRecordDao
    
    companion object {
        @Volatile
        private var INSTANCE: AppDatabase? = null
        
        fun getDatabase(context: Context): AppDatabase {
            return INSTANCE ?: synchronized(this) {
                val instance = Room.databaseBuilder(
                    context.applicationContext,
                    AppDatabase::class.java,
                    "team_database"
                )
                .fallbackToDestructiveMigration() // For development: recreate DB on schema change
                .build()
                INSTANCE = instance
                instance
            }
        }
    }
}
