package com.meshcore.team.data.database

import androidx.room.TypeConverter

/**
 * Type converters for Room database
 * Handles ByteArray conversions
 */
class Converters {
    
    @TypeConverter
    fun fromByteArray(value: ByteArray?): String? {
        return value?.joinToString(",") { it.toString() }
    }
    
    @TypeConverter
    fun toByteArray(value: String?): ByteArray? {
        return value?.split(",")?.map { it.toByte() }?.toByteArray()
    }
}
