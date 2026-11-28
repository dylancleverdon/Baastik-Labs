package com.baastiklabs.pickless.health

import android.content.Context
import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.permission.HealthPermission
import androidx.health.connect.client.records.ExerciseSessionRecord
import androidx.health.connect.client.records.MenstruationFlowRecord
import androidx.health.connect.client.records.SleepSessionRecord
import androidx.health.connect.client.request.ReadRecordsRequest
import androidx.health.connect.client.time.TimeRangeFilter
import com.baastiklabs.pickless.log.AppLog
import java.time.Duration
import java.time.LocalDate
import java.time.ZoneId

/**
 * Optional: read sleep, exercise and period days from Health Connect (if a watch or fitness app
 * writes them) to pre-fill the daily check-in. Read-only; nothing is written back.
 */
object Health {
    val permissions = setOf(
        HealthPermission.getReadPermission(SleepSessionRecord::class),
        HealthPermission.getReadPermission(ExerciseSessionRecord::class),
        HealthPermission.getReadPermission(MenstruationFlowRecord::class),
    )

    fun available(context: Context): Boolean = runCatching { HealthConnectClient.getSdkStatus(context) == HealthConnectClient.SDK_AVAILABLE }.getOrDefault(false)

    suspend fun granted(context: Context): Set<String> = if (!available(context)) emptySet() else runCatching {
        HealthConnectClient.getOrCreate(context).permissionController.getGrantedPermissions()
    }.getOrDefault(emptySet())

    data class Day(val hours: Double?, val exerciseLevel: Int?, val period: Boolean?)

    /** Sleep that ended on [epochDay] (the night before), plus that day's exercise and period. */
    suspend fun lastNight(context: Context, epochDay: Long): Day? {
        if (!available(context)) return null
        return runCatching {
            val client = HealthConnectClient.getOrCreate(context)
            val granted = client.permissionController.getGrantedPermissions()
            val zone = ZoneId.systemDefault()
            val day = LocalDate.ofEpochDay(epochDay)
            val sleepRange = TimeRangeFilter.between(day.minusDays(1).atTime(18, 0).atZone(zone).toInstant(), day.atTime(14, 0).atZone(zone).toInstant())
            val dayRange = TimeRangeFilter.between(day.atStartOfDay(zone).toInstant(), day.plusDays(1).atStartOfDay(zone).toInstant())
            val hours = if (HealthPermission.getReadPermission(SleepSessionRecord::class) in granted) {
                client.readRecords(ReadRecordsRequest(SleepSessionRecord::class, sleepRange)).records
                    .sumOf { Duration.between(it.startTime, it.endTime).toMinutes() }.takeIf { it > 0 }?.let { it / 60.0 }
            } else null
            val exercise = if (HealthPermission.getReadPermission(ExerciseSessionRecord::class) in granted) {
                val minutes = client.readRecords(ReadRecordsRequest(ExerciseSessionRecord::class, dayRange)).records.sumOf { Duration.between(it.startTime, it.endTime).toMinutes() }
                when { minutes >= 60 -> 3; minutes >= 30 -> 2; minutes > 0 -> 1; else -> null }
            } else null
            val period = if (HealthPermission.getReadPermission(MenstruationFlowRecord::class) in granted) {
                client.readRecords(ReadRecordsRequest(MenstruationFlowRecord::class, dayRange)).records.isNotEmpty().takeIf { it }
            } else null
            Day(hours, exercise, period)
        }.onFailure { AppLog.w("health", "read failed: ${it.javaClass.simpleName}") }.getOrNull()
    }
}
