package com.baastiklabs.pickless.core.detective

import com.baastiklabs.pickless.core.fodmap.FodmapGroup
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId

/** Everything the detective reads, already flattened from the app's database. Times are epoch millis. */
data class Diary(
    val meals: List<MealLog>,
    val bms: List<BmLog>,
    val symptoms: List<SymptomLog> = emptyList(),
    val days: List<DayLog> = emptyList(),
    val events: List<QuickEvent> = emptyList(),
    val zone: ZoneId = ZoneId.systemDefault(),
) {
    fun dayOf(time: Long): Long = Instant.ofEpochMilli(time).atZone(zone).toLocalDate().toEpochDay()
    fun startOfDay(epochDay: Long): Long = LocalDate.ofEpochDay(epochDay).atStartOfDay(zone).toInstant().toEpochMilli()
    fun hourOf(time: Long): Int = Instant.ofEpochMilli(time).atZone(zone).hour

    val dayMap: Map<Long, DayLog> by lazy { days.associateBy { it.epochDay } }

    /** Only data up to [time] (for "how did the picture look a week ago"). */
    fun until(time: Long): Diary = copy(
        meals = meals.filter { it.time <= time }, bms = bms.filter { it.time <= time },
        symptoms = symptoms.filter { it.time <= time }, days = days.filter { startOfDay(it.epochDay) <= time },
        events = events.filter { it.time <= time },
    )
}

/**
 * One eating occasion. [keys] are detective features ("food:oat-milk", "ing:garlic", "tag:dairy");
 * [fodmapUnits] is the FODMAP load per group in low-serving units; [avoidHits] are avoid-list
 * items it broke; [names] are what was eaten, for showing evidence.
 */
data class MealLog(
    val id: Long,
    val time: Long,
    val keys: Set<String>,
    val fodmapUnits: Map<FodmapGroup, Double> = emptyMap(),
    val avoidHits: Set<String> = emptySet(),
    val names: List<String> = emptyList(),
    val grams: Double = 0.0,
)

data class BmLog(
    val id: Long,
    val time: Long,
    /** Bristol Stool Scale 1-7. */
    val bristol: Int,
    /** 0 none, 1 some, 2 rushed, 3 couldn't wait. */
    val urgency: Int = 0,
    val accident: Boolean = false,
    val pain: Int = 0,
    val blood: Boolean = false,
)

data class SymptomLog(val id: Long, val time: Long, val type: String, val severity: Int)

enum class Nicotine(val id: String, val label: String) {
    NONE("none", "None"), LESS("less", "Less than usual"), USUAL("usual", "Usual"), MORE("more", "More than usual");

    companion object { fun byId(id: String?): Nicotine? = entries.firstOrNull { it.id == id } }
}

/** The daily check-in. Nulls are "not answered". */
data class DayLog(
    val epochDay: Long,
    val overall: Int? = null,
    val stress: Int? = null,
    val sleepQuality: Int? = null,
    val sleepHours: Double? = null,
    val nicotine: Nicotine? = null,
    val alcoholDrinks: Int? = null,
    val caffeineCups: Int? = null,
    val exercise: Int? = null,
    val period: Boolean? = null,
    val sick: Boolean = false,
    val travel: Boolean = false,
    val noBm: Boolean? = null,
    val checkedIn: Boolean = true,
)

/** Quick-logged things with a time: "coffee", "alcohol", "nicotine", "water". */
data class QuickEvent(val time: Long, val type: String, val amount: Double = 1.0)

/** What counts as a bad BM. The owner's are mostly loose. */
data class BadBm(
    val looseFrom: Int = 6,
    val urgentFiveCounts: Boolean = true,
    val hardUpTo: Int? = null,
) {
    fun isBad(b: BmLog): Boolean = b.accident || b.bristol >= looseFrom ||
        (urgentFiveCounts && b.bristol == 5 && b.urgency >= 2) || (hardUpTo != null && b.bristol <= hardUpTo)

    fun describe(b: BmLog): String = when {
        b.accident -> "accident"
        b.bristol >= 6 -> "loose BM (type ${b.bristol})"
        b.bristol == 5 -> "urgent soft BM (type 5)"
        b.bristol <= 2 -> "hard BM (type ${b.bristol})"
        else -> "BM type ${b.bristol}"
    }

    companion object {
        val MOSTLY_LOOSE = BadBm()
        val MOSTLY_HARD = BadBm(looseFrom = 8, urgentFiveCounts = false, hardUpTo = 2)
        val BOTH = BadBm(hardUpTo = 2)
    }
}
