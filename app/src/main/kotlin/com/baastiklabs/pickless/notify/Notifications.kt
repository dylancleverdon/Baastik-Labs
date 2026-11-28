package com.baastiklabs.pickless.notify

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import com.baastiklabs.pickless.MainActivity
import com.baastiklabs.pickless.R

object Notifications {
    const val REMINDERS = "reminders"
    const val UPDATES = "updates"
    const val COACH = "coach"

    fun createChannels(context: Context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val nm = context.getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(REMINDERS, "Check-ins and reminders", NotificationManager.IMPORTANCE_DEFAULT).apply { description = "Evening check-in, after-meal nudges, weekly check-in" })
        nm.createNotificationChannel(NotificationChannel(UPDATES, "Updates", NotificationManager.IMPORTANCE_LOW).apply { description = "When a new version of Pickless is ready" })
        nm.createNotificationChannel(NotificationChannel(COACH, "Food tests", NotificationManager.IMPORTANCE_DEFAULT).apply { description = "Reintroduction test days" })
    }

    private fun allowed(context: Context) = Build.VERSION.SDK_INT < 33 ||
        ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED

    fun open(context: Context, route: String, code: Int): PendingIntent = PendingIntent.getActivity(
        context, code, Intent(context, MainActivity::class.java).putExtra(MainActivity.EXTRA_ROUTE, route).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )

    @android.annotation.SuppressLint("MissingPermission") // allowed() checks POST_NOTIFICATIONS first
    private fun post(context: Context, id: Int, channel: String, title: String, text: String, intent: PendingIntent) {
        if (!allowed(context)) return
        val n = NotificationCompat.Builder(context, channel)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(title).setContentText(text)
            .setStyle(NotificationCompat.BigTextStyle().bigText(text))
            .setContentIntent(intent).setAutoCancel(true)
            .build()
        runCatching { NotificationManagerCompat.from(context).notify(id, n) }
    }

    fun checkIn(context: Context) = post(context, 10, REMINDERS, "Evening check-in", "20 seconds: how was your gut today? Sleep, stress, nicotine and any BMs.", open(context, "checkin", 10))
    fun nudge(context: Context, meal: String) = post(context, 11, REMINDERS, "How's your gut?", "A few hours since $meal. Any bloating, pain or a BM? One tap to log it.", open(context, "symptom", 11))
    fun weekly(context: Context) = post(context, 12, REMINDERS, "Weekly check-in", "Five quick questions to see whether the diet is helping overall.", open(context, "weekly", 12))
    fun coach(context: Context, text: String) = post(context, 13, COACH, "Food test", text, open(context, "coach", 13))
    fun updateAvailable(context: Context, version: String) = post(context, 20, UPDATES, "Pickless $version is available", "Open Settings to install it.", open(context, "settings", 20))
    fun allowUpdates(context: Context) {
        val i = PendingIntent.getActivity(context, 21, com.baastiklabs.pickless.update.Updater(context).permissionIntent(), PendingIntent.FLAG_IMMUTABLE)
        post(context, 21, UPDATES, "Let Pickless update itself", "Tap, then turn on \"Allow from this source\" so updates can install.", i)
    }
    fun confirmInstall(context: Context, version: String, confirm: Intent) {
        val i = PendingIntent.getActivity(context, 22, confirm, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        post(context, 22, UPDATES, "Update ready: Pickless $version", "Tap to install.", i)
    }
}
