package com.baastiklabs.pickless.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.longPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.core.stringSetPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import com.baastiklabs.pickless.core.detective.BadBm
import com.baastiklabs.pickless.core.fodmap.FodmapGroup
import com.baastiklabs.pickless.core.kb.AvoidItem
import com.baastiklabs.pickless.core.kb.AvoidStatus
import com.baastiklabs.pickless.core.kb.Categories
import com.baastiklabs.pickless.core.kb.Strictness
import com.baastiklabs.pickless.core.kb.TracePolicy
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.time.LocalDate
import java.time.YearMonth

private val Context.store: DataStore<Preferences> by preferencesDataStore(name = "settings")

@Serializable
data class StoredAvoid(
    val key: String, val label: String, val categoryId: String? = null, val foodId: String? = null, val term: String? = null,
    val status: String = "avoiding", val strictness: String = "STANDARD", val traces: String = "CAUTION", val since: Long? = null,
) {
    fun toItem() = AvoidItem(key, label, categoryId, foodId, term, AvoidStatus.byId(status),
        runCatching { Strictness.valueOf(strictness) }.getOrDefault(Strictness.STANDARD),
        runCatching { TracePolicy.valueOf(traces) }.getOrDefault(TracePolicy.CAUTION), since)

    companion object {
        fun of(i: AvoidItem) = StoredAvoid(i.key, i.label, i.categoryId, i.foodId, i.term, i.status.id, i.strictness.name, i.traces.name, i.sinceEpochDay)
    }
}

data class Prefs(
    val onboarded: Boolean = false,
    val avoid: List<AvoidItem> = emptyList(),
    val badBm: String = "loose",
    val checkinHour: Int = 20,
    val checkinMinute: Int = 30,
    val nudges: Boolean = true,
    val nudgeHours: Int = 3,
    val weeklyReminder: Boolean = true,
    val nicotineTracking: Boolean = true,
    val nicotineUsual: Int = 0,
    val cycleTracking: Boolean = false,
    val braveKey: String = "",
    val braveCap: Int = 300,
    val braveMonth: String = "",
    val braveCount: Int = 0,
    val suggestEndpoint: String = "",
    val manifestEndpoint: String = "",
    val suggestFrom: String = "Owner",
    val appLock: Boolean = false,
    val healthConnect: Boolean = false,
    val tolerance: Map<FodmapGroup, Double> = emptyMap(),
    val favorites: Set<String> = emptySet(),
    val autoUpdate: Boolean = true,
    val packVersion: String = "",
    val meds: List<String> = listOf("Probiotic", "Fiber supplement", "Magnesium", "Loperamide (Imodium)", "Antacid"),
    val lastUpdateCheck: Long = 0,
    val crashSeen: Long = 0,
) {
    val badBmRule: BadBm get() = when (badBm) { "hard" -> BadBm.MOSTLY_HARD; "both" -> BadBm.BOTH; else -> BadBm.MOSTLY_LOOSE }
    val endpoint: String get() = suggestEndpoint.ifBlank { manifestEndpoint }
    val braveUsedThisMonth: Int get() = if (braveMonth == YearMonth.now().toString()) braveCount else 0
    val braveAvailable: Boolean get() = braveKey.isNotBlank() && braveUsedThisMonth < braveCap
}

class Settings(private val context: Context) {
    private val json = Json { ignoreUnknownKeys = true }

    private object K {
        val onboarded = booleanPreferencesKey("onboarded")
        val avoid = stringPreferencesKey("avoid_list")
        val badBm = stringPreferencesKey("bad_bm")
        val checkinHour = intPreferencesKey("checkin_hour")
        val checkinMinute = intPreferencesKey("checkin_minute")
        val nudges = booleanPreferencesKey("nudges")
        val nudgeHours = intPreferencesKey("nudge_hours")
        val weekly = booleanPreferencesKey("weekly_reminder")
        val nicotineTracking = booleanPreferencesKey("nicotine_tracking")
        val nicotineUsual = intPreferencesKey("nicotine_usual")
        val cycle = booleanPreferencesKey("cycle_tracking")
        val braveKey = stringPreferencesKey("brave_key")
        val braveCap = intPreferencesKey("brave_cap")
        val braveMonth = stringPreferencesKey("brave_month")
        val braveCount = intPreferencesKey("brave_count")
        val endpoint = stringPreferencesKey("suggest_endpoint")
        val manifestEndpoint = stringPreferencesKey("manifest_endpoint")
        val from = stringPreferencesKey("suggest_from")
        val appLock = booleanPreferencesKey("app_lock")
        val health = booleanPreferencesKey("health_connect")
        val tolerance = stringPreferencesKey("tolerance")
        val favorites = stringSetPreferencesKey("favorites")
        val autoUpdate = booleanPreferencesKey("auto_update")
        val packVersion = stringPreferencesKey("pack_version")
        val meds = stringPreferencesKey("meds")
        val lastUpdateCheck = longPreferencesKey("last_update_check")
        val crashSeen = longPreferencesKey("crash_seen")
    }

    val prefs: Flow<Prefs> = context.store.data.map { p ->
        val d = Prefs()
        Prefs(
            onboarded = p[K.onboarded] ?: false,
            avoid = p[K.avoid]?.let { s -> runCatching { json.decodeFromString<List<StoredAvoid>>(s).map { it.toItem() } }.getOrNull() } ?: defaultAvoid(),
            badBm = p[K.badBm] ?: d.badBm,
            checkinHour = p[K.checkinHour] ?: d.checkinHour,
            checkinMinute = p[K.checkinMinute] ?: d.checkinMinute,
            nudges = p[K.nudges] ?: d.nudges,
            nudgeHours = p[K.nudgeHours] ?: d.nudgeHours,
            weeklyReminder = p[K.weekly] ?: d.weeklyReminder,
            nicotineTracking = p[K.nicotineTracking] ?: d.nicotineTracking,
            nicotineUsual = p[K.nicotineUsual] ?: d.nicotineUsual,
            cycleTracking = p[K.cycle] ?: d.cycleTracking,
            braveKey = p[K.braveKey] ?: "",
            braveCap = p[K.braveCap] ?: d.braveCap,
            braveMonth = p[K.braveMonth] ?: "",
            braveCount = p[K.braveCount] ?: 0,
            suggestEndpoint = p[K.endpoint] ?: "",
            manifestEndpoint = p[K.manifestEndpoint] ?: "",
            suggestFrom = p[K.from] ?: d.suggestFrom,
            appLock = p[K.appLock] ?: false,
            healthConnect = p[K.health] ?: false,
            tolerance = p[K.tolerance]?.let { s ->
                runCatching { json.decodeFromString<Map<String, Double>>(s).mapNotNull { (k, v) -> FodmapGroup.byId(k)?.let { it to v } }.toMap() }.getOrNull()
            } ?: emptyMap(),
            favorites = p[K.favorites] ?: emptySet(),
            autoUpdate = p[K.autoUpdate] ?: true,
            packVersion = p[K.packVersion] ?: "",
            meds = p[K.meds]?.let { s -> runCatching { json.decodeFromString<List<String>>(s) }.getOrNull() } ?: d.meds,
            lastUpdateCheck = p[K.lastUpdateCheck] ?: 0,
            crashSeen = p[K.crashSeen] ?: 0,
        )
    }

    suspend fun current(): Prefs = prefs.first()

    private fun defaultAvoid(): List<AvoidItem> {
        val today = LocalDate.now().toEpochDay()
        return Categories.defaultAvoidList.map { AvoidItem.ofCategory(it, today) }
    }

    suspend fun setAvoid(items: List<AvoidItem>) = context.store.edit { it[K.avoid] = json.encodeToString(items.map(StoredAvoid::of)) }
    suspend fun updateAvoid(transform: (List<AvoidItem>) -> List<AvoidItem>) = setAvoid(transform(current().avoid))
    suspend fun setOnboarded(v: Boolean) = context.store.edit { it[K.onboarded] = v }
    suspend fun setBadBm(v: String) = context.store.edit { it[K.badBm] = v }
    suspend fun setCheckinTime(h: Int, m: Int) = context.store.edit { it[K.checkinHour] = h; it[K.checkinMinute] = m }
    suspend fun setNudges(on: Boolean, hours: Int) = context.store.edit { it[K.nudges] = on; it[K.nudgeHours] = hours }
    suspend fun setWeekly(on: Boolean) = context.store.edit { it[K.weekly] = on }
    suspend fun setNicotine(tracking: Boolean, usual: Int) = context.store.edit { it[K.nicotineTracking] = tracking; it[K.nicotineUsual] = usual }
    suspend fun setCycle(on: Boolean) = context.store.edit { it[K.cycle] = on }
    suspend fun setBrave(key: String, cap: Int) = context.store.edit { it[K.braveKey] = key.trim(); it[K.braveCap] = cap }
    suspend fun countBrave() = context.store.edit {
        val month = YearMonth.now().toString()
        if (it[K.braveMonth] != month) { it[K.braveMonth] = month; it[K.braveCount] = 0 }
        it[K.braveCount] = (it[K.braveCount] ?: 0) + 1
    }
    suspend fun setEndpoint(url: String) = context.store.edit { it[K.endpoint] = url.trim() }
    suspend fun setManifestEndpoint(url: String) = context.store.edit { it[K.manifestEndpoint] = url.trim() }
    suspend fun setFrom(name: String) = context.store.edit { it[K.from] = name.trim().ifEmpty { "Owner" } }
    suspend fun setAppLock(v: Boolean) = context.store.edit { it[K.appLock] = v }
    suspend fun setHealthConnect(v: Boolean) = context.store.edit { it[K.health] = v }
    suspend fun setTolerance(group: FodmapGroup, factor: Double?) = context.store.edit {
        val cur = current().tolerance.toMutableMap()
        if (factor == null) cur.remove(group) else cur[group] = factor
        it[K.tolerance] = json.encodeToString(cur.mapKeys { (g, _) -> g.id })
    }
    suspend fun toggleFavorite(ref: String) = context.store.edit {
        val cur = it[K.favorites] ?: emptySet()
        it[K.favorites] = if (ref in cur) cur - ref else cur + ref
    }
    suspend fun setAutoUpdate(v: Boolean) = context.store.edit { it[K.autoUpdate] = v }
    suspend fun setPackVersion(v: String) = context.store.edit { it[K.packVersion] = v }
    suspend fun setMeds(list: List<String>) = context.store.edit { it[K.meds] = json.encodeToString(list) }
    suspend fun setLastUpdateCheck(t: Long) = context.store.edit { it[K.lastUpdateCheck] = t }
    suspend fun setCrashSeen(t: Long) = context.store.edit { it[K.crashSeen] = t }

    /** Everything, for the backup file. */
    suspend fun export(): Map<String, String> = context.store.data.first().asMap().mapKeys { it.key.name }.mapValues { it.value.toString() }
}
