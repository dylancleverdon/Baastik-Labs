package com.baastiklabs.pickless

import android.app.Application
import android.content.Context
import com.baastiklabs.pickless.core.kb.KnowledgeBase
import com.baastiklabs.pickless.data.CoachService
import com.baastiklabs.pickless.data.Insights
import com.baastiklabs.pickless.data.PicklessDb
import com.baastiklabs.pickless.data.Repository
import com.baastiklabs.pickless.data.Settings
import com.baastiklabs.pickless.log.AppLog
import com.baastiklabs.pickless.net.Products
import com.baastiklabs.pickless.net.UsdaPack
import com.baastiklabs.pickless.net.WebLookup
import com.baastiklabs.pickless.notify.Notifications
import com.baastiklabs.pickless.suggest.SuggestionBox
import com.baastiklabs.pickless.update.Updater
import com.baastiklabs.pickless.work.Jobs
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

class Container(context: Context) {
    val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    val settings = Settings(context)
    val db = PicklessDb.open(context)
    val kb: KnowledgeBase by lazy { KnowledgeBase.load() }
    val pack = UsdaPack(context, settings)
    val products = Products(db, pack)
    val web = WebLookup(db, settings)
    val repo: Repository by lazy { Repository(kb, db, settings, products, web) }
    val suggestions = SuggestionBox(context, db, settings)
    val updater = Updater(context)
    val insights: Insights by lazy { Insights(repo, settings, scope) }
    val coach: CoachService by lazy { CoachService(context, repo, settings, insights) }
}

class PicklessApp : Application(), androidx.work.Configuration.Provider {
    override val workManagerConfiguration: androidx.work.Configuration
        get() = androidx.work.Configuration.Builder().setMinimumLoggingLevel(android.util.Log.INFO).build()

    lateinit var container: Container
        private set

    override fun onCreate() {
        super.onCreate()
        AppLog.init(this)
        Notifications.createChannels(this)
        container = Container(this)
        container.scope.launch {
            container.kb // warm up the knowledge base off the main thread
            val p = container.settings.current()
            runCatching { Jobs.scheduleAll(this@PicklessApp, p.checkinHour, p.checkinMinute, p.weeklyReminder) }
            runCatching { container.coach.evaluate() }
            runCatching { container.insights.refresh() }
        }
    }
}
