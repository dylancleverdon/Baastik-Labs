package com.baastiklabs.pickless.work

import android.content.Context
import androidx.work.BackoffPolicy
import androidx.work.Constraints
import androidx.work.CoroutineWorker
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.ExistingWorkPolicy
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.WorkerParameters
import androidx.work.workDataOf
import com.baastiklabs.pickless.PicklessApp
import com.baastiklabs.pickless.log.AppLog
import com.baastiklabs.pickless.notify.Notifications
import java.time.DayOfWeek
import java.time.Duration
import java.time.LocalDateTime
import java.util.concurrent.TimeUnit

/** Background jobs: updates, the product pack, the suggestion queue and reminders. */
object Jobs {
    private val online = Constraints.Builder().setRequiredNetworkType(NetworkType.CONNECTED).build()
    private val wifiOnly = Constraints.Builder().setRequiredNetworkType(NetworkType.UNMETERED).build()
    private val wifi = Constraints.Builder().setRequiredNetworkType(NetworkType.UNMETERED).setRequiresStorageNotLow(true).build()

    fun scheduleAll(context: Context, checkinHour: Int, checkinMinute: Int, weekly: Boolean) {
        val wm = WorkManager.getInstance(context)
        // Hourly, on Wi-Fi (an APK is ~30 MB); "Check now" in Settings works on any network.
        wm.enqueueUniquePeriodicWork("update", ExistingPeriodicWorkPolicy.UPDATE,
            PeriodicWorkRequestBuilder<UpdateWorker>(1, TimeUnit.HOURS).setConstraints(wifiOnly).setInitialDelay(2, TimeUnit.MINUTES).build())
        wm.enqueueUniquePeriodicWork("pack", ExistingPeriodicWorkPolicy.KEEP,
            PeriodicWorkRequestBuilder<PackWorker>(1, TimeUnit.DAYS).setConstraints(wifi).build())
        wm.enqueueUniquePeriodicWork("suggestions", ExistingPeriodicWorkPolicy.KEEP,
            PeriodicWorkRequestBuilder<SuggestionWorker>(6, TimeUnit.HOURS).setConstraints(online).build())
        scheduleCheckIn(context, checkinHour, checkinMinute)
        if (weekly) scheduleWeekly(context) else wm.cancelUniqueWork("weekly")
    }

    private fun delayTo(hour: Int, minute: Int, dayOfWeek: DayOfWeek? = null): Long {
        val now = LocalDateTime.now()
        var next = now.withHour(hour).withMinute(minute).withSecond(0).withNano(0)
        if (dayOfWeek != null) while (next.dayOfWeek != dayOfWeek || !next.isAfter(now)) next = next.plusDays(1)
        else if (!next.isAfter(now)) next = next.plusDays(1)
        return Duration.between(now, next).toMillis()
    }

    fun scheduleCheckIn(context: Context, hour: Int, minute: Int) {
        WorkManager.getInstance(context).enqueueUniquePeriodicWork("checkin", ExistingPeriodicWorkPolicy.UPDATE,
            PeriodicWorkRequestBuilder<ReminderWorker>(1, TimeUnit.DAYS).setInitialDelay(delayTo(hour, minute), TimeUnit.MILLISECONDS)
                .setInputData(workDataOf("kind" to "checkin")).build())
    }

    private fun scheduleWeekly(context: Context) {
        WorkManager.getInstance(context).enqueueUniquePeriodicWork("weekly", ExistingPeriodicWorkPolicy.UPDATE,
            PeriodicWorkRequestBuilder<ReminderWorker>(7, TimeUnit.DAYS).setInitialDelay(delayTo(19, 0, DayOfWeek.SUNDAY), TimeUnit.MILLISECONDS)
                .setInputData(workDataOf("kind" to "weekly")).build())
    }

    fun nudgeAfterMeal(context: Context, meal: String, hours: Int) {
        val hour = LocalDateTime.now().plusHours(hours.toLong()).hour
        if (hour >= 23 || hour < 7) return
        WorkManager.getInstance(context).enqueueUniqueWork("nudge", ExistingWorkPolicy.REPLACE,
            OneTimeWorkRequestBuilder<ReminderWorker>().setInitialDelay(hours.toLong(), TimeUnit.HOURS)
                .setInputData(workDataOf("kind" to "nudge", "meal" to meal)).build())
    }

    fun sendSuggestions(context: Context) {
        WorkManager.getInstance(context).enqueueUniqueWork("send-suggestions", ExistingWorkPolicy.REPLACE,
            OneTimeWorkRequestBuilder<SuggestionWorker>().setConstraints(online).setBackoffCriteria(BackoffPolicy.EXPONENTIAL, 30, TimeUnit.SECONDS).build())
    }

    fun checkUpdatesNow(context: Context) {
        WorkManager.getInstance(context).enqueueUniqueWork("update-now", ExistingWorkPolicy.KEEP,
            OneTimeWorkRequestBuilder<UpdateWorker>().setConstraints(online).build())
    }

    fun refreshPackNow(context: Context) {
        WorkManager.getInstance(context).enqueueUniqueWork("pack-now", ExistingWorkPolicy.KEEP,
            OneTimeWorkRequestBuilder<PackWorker>().setConstraints(online).build())
    }
}

class UpdateWorker(context: Context, params: WorkerParameters) : CoroutineWorker(context, params) {
    override suspend fun doWork(): Result {
        val app = applicationContext as PicklessApp
        runCatching { app.container.updater.check() }.onFailure { AppLog.e("update", "check failed", it) }
        return Result.success()
    }
}

class PackWorker(context: Context, params: WorkerParameters) : CoroutineWorker(context, params) {
    override suspend fun doWork(): Result {
        val app = applicationContext as PicklessApp
        val msg = app.container.pack.refresh()
        AppLog.i("pack", msg)
        return if (msg.contains("retry")) Result.retry() else Result.success()
    }
}

class SuggestionWorker(context: Context, params: WorkerParameters) : CoroutineWorker(context, params) {
    override suspend fun doWork(): Result {
        val app = applicationContext as PicklessApp
        val left = app.container.suggestions.sendPending()
        val connected = app.container.settings.current().endpoint.isNotBlank()
        return if (left > 0 && connected && runAttemptCount < 8) Result.retry() else Result.success()
    }
}

class ReminderWorker(context: Context, params: WorkerParameters) : CoroutineWorker(context, params) {
    override suspend fun doWork(): Result {
        val app = applicationContext as PicklessApp
        val c = app.container
        when (inputData.getString("kind")) {
            "checkin" -> if (c.db.checkins().get(c.repo.today()) == null) Notifications.checkIn(applicationContext)
            "weekly" -> Notifications.weekly(applicationContext)
            "nudge" -> {
                val recent = System.currentTimeMillis() - 2 * 3_600_000L
                val logged = c.db.symptoms().allOnce().any { it.time > recent } || c.db.bm().allOnce().any { it.time > recent }
                if (!logged && c.settings.current().nudges) Notifications.nudge(applicationContext, inputData.getString("meal") ?: "your meal")
            }
        }
        c.coach.dailyNotice()
        return Result.success()
    }
}
