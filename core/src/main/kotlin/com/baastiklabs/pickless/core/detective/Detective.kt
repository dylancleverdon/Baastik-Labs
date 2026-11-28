package com.baastiklabs.pickless.core.detective

import com.baastiklabs.pickless.core.fodmap.FodmapGroup
import java.util.SplittableRandom
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.sqrt

enum class Tier(val label: String, val rank: Int) {
    STRONG("Strong", 4), MODERATE("Moderate", 3), WEAK("Weak", 2), EARLY("Very early hint", 1), NONE("Not enough data", 0)
}

enum class FeatureKind { FOOD, INGREDIENT, FAMILY, AVOID, FODMAP, LIFESTYLE, PATTERN }

data class EvidenceDay(val epochDay: Long, val eaten: List<String>, val bad: Boolean, val outcome: String, val lagHours: Double?)

data class Suspect(
    val key: String,
    val label: String,
    val kind: FeatureKind,
    val tier: Tier,
    /** Probability that bad BMs really are more likely after it (Bayesian, with shrinkage). */
    val probHarm: Double,
    /** How many times more likely a bad BM is after it than on other days. */
    val ratio: Double,
    val exposedDays: Int,
    val badAfter: Int,
    val exposedRate: Double,
    val baselineRate: Double,
    val typicalLagHours: Double?,
    val sameAs: List<String>,
    /** Narrower things it explains (e.g. "garlic" explains pizza and pasta). */
    val includes: List<String> = emptyList(),
    val tangledWith: List<String>,
    val separation: String?,
    val evidence: List<EvidenceDay>,
    val outcome: String = "bad BMs",
)

data class SafeFood(val key: String, val label: String, val exposedDays: Int, val badAfter: Int)

data class DataQuality(
    val daysLogged: Int,
    val completeness: Double,
    val ratedBms: Int,
    val badBms: Int,
    val stable: Boolean,
    val missing: List<String>,
) {
    /** When true the "early read" banner goes away. */
    val solid: Boolean get() = missing.isEmpty()
}

data class NextTest(val key: String, val label: String, val why: String)

data class BmPattern(
    val count: Int,
    val days: Int,
    val perDay: Double,
    val byType: Map<Int, Int>,
    val hardShare: Double,
    val looseShare: Double,
    val urgentShare: Double,
    val accidents: Int,
    val pattern: String,
)

data class Report(
    val generatedAt: Long,
    val suspects: List<Suspect>,
    val lifestyle: List<Suspect>,
    val symptomSuspects: List<Suspect>,
    val safe: List<SafeFood>,
    val quality: DataQuality,
    val baselineRate: Double,
    val analyzedDays: Int,
    val nextTest: NextTest?,
    val bmPattern: BmPattern,
    private val statsByKey: Map<String, Stat>,
) {
    data class Stat(val probHarm: Double, val ratio: Double, val exposed: Int, val bad: Int)

    fun statOf(key: String): Stat? = statsByKey[key]
}

data class Culprit(val key: String, val label: String, val score: Double, val reason: String, val hoursBefore: Double?, val history: String?)

data class Explanation(val bm: BmLog, val bad: Boolean, val culprits: List<Culprit>, val lifestyle: List<Culprit>, val summary: String)

/**
 * The trigger detective. Compares how often a bad BM follows days with each food, ingredient,
 * FODMAP load or lifestyle factor against days without it, with Bayesian shrinkage so a food eaten
 * twice can't look like a sure thing, a false-discovery correction across everything tested, and
 * untangling of things that are always eaten together. It starts from the first log; until the data
 * is solid the report says so ([DataQuality.solid]).
 */
class Detective(
    private val labeler: (String) -> String,
    private val badBm: BadBm = BadBm.MOSTLY_LOOSE,
) {
    companion object {
        const val HOUR = 3_600_000L
        const val DAY = 24 * HOUR
        private const val PRIOR_STRENGTH = 4.0
        private const val DRAWS = 3000

        fun kindOf(key: String): FeatureKind = when (key.substringBefore(':')) {
            "food" -> FeatureKind.FOOD
            "ing" -> FeatureKind.INGREDIENT
            "tag" -> FeatureKind.FAMILY
            "avoid" -> FeatureKind.AVOID
            "fodmap" -> FeatureKind.FODMAP
            "life" -> FeatureKind.LIFESTYLE
            else -> FeatureKind.PATTERN
        }

        /** Labels for the detective's own features; the app labels foods and tags. */
        fun builtInLabel(key: String): String? = when {
            key.startsWith("fodmap:") -> FodmapGroup.byId(key.substringAfter(':'))?.let { "High-${it.label.lowercase()} meals" }
            key == "life:poor-sleep" -> "Poor sleep the night before"
            key == "life:stress" -> "Stressful days"
            key == "life:nicotine-more" -> "More nicotine than usual"
            key == "life:nicotine-less" -> "Less nicotine than usual"
            key == "life:nicotine-none" -> "No nicotine"
            key == "life:alcohol" -> "2+ alcoholic drinks"
            key == "life:caffeine-high" -> "3+ caffeinated drinks"
            key == "life:exercise-hard" -> "Hard exercise"
            key == "life:period" -> "Period days"
            key == "pattern:late-meal" -> "Eating late (after 9 pm)"
            key == "pattern:big-meal" -> "Big meals"
            key == "pattern:high-fodmap-day" -> "High-FODMAP days (stacked)"
            else -> null
        }
    }

    private fun label(key: String) = builtInLabel(key) ?: labeler(key)

    private class DayRow(val day: Long, val features: Set<String>, val firstExposure: Map<String, Long>, val bad: Boolean, val badTime: Long?, val badDesc: String?, val firstMeal: Long)

    // ------------------------------------------------------------------ dataset

    private fun dayRows(diary: Diary, now: Long): List<DayRow> {
        val mealsByDay = diary.meals.groupBy { diary.dayOf(it.time) }
        val eventsByDay = diary.events.groupBy { diary.dayOf(it.time) }
        val bms = diary.bms.sortedBy { it.time }
        val out = mutableListOf<DayRow>()
        for ((day, meals) in mealsByDay.toSortedMap()) {
            val ctx = diary.dayMap[day]
            if (ctx != null && (ctx.sick || ctx.travel)) continue
            val first = meals.minOf { it.time }
            val windowStart = first + HOUR
            val windowEnd = diary.startOfDay(day + 1) + 18 * HOUR
            val badEvent = bms.firstOrNull { it.time in windowStart..windowEnd && badBm.isBad(it) }
            if (badEvent == null && windowEnd > now) continue // outcome not known yet
            val feats = mutableSetOf<String>()
            val firstExp = mutableMapOf<String, Long>()
            fun add(k: String, t: Long) { feats += k; if ((firstExp[k] ?: Long.MAX_VALUE) > t) firstExp[k] = t }
            val dayUnits = mutableMapOf<FodmapGroup, Double>()
            for (m in meals.sortedBy { it.time }) {
                m.keys.forEach { add(it, m.time) }
                m.avoidHits.forEach { add("avoid:$it", m.time) }
                for ((g, u) in m.fodmapUnits) {
                    dayUnits[g] = (dayUnits[g] ?: 0.0) + u
                    if (u >= 1.0) add("fodmap:${g.id}", m.time)
                }
                if (diary.hourOf(m.time) >= 21) add("pattern:late-meal", m.time)
                if (m.grams >= 800 || m.names.size >= 5) add("pattern:big-meal", m.time)
            }
            for ((g, u) in dayUnits) if (u >= 1.5) add("fodmap:${g.id}", first)
            if (dayUnits.values.sumOf { max(0.0, it) } >= 3.0) add("pattern:high-fodmap-day", first)
            eventsByDay[day]?.forEach { e ->
                when (e.type) {
                    "coffee" -> add("tag:caffeine", e.time)
                    "alcohol" -> add("tag:alcohol", e.time)
                }
            }
            if (ctx != null) {
                if ((ctx.sleepQuality != null && ctx.sleepQuality <= 1) || (ctx.sleepHours != null && ctx.sleepHours < 6)) add("life:poor-sleep", first)
                if (ctx.stress != null && ctx.stress >= 3) add("life:stress", first)
                when (ctx.nicotine) {
                    Nicotine.MORE -> add("life:nicotine-more", first)
                    Nicotine.LESS -> add("life:nicotine-less", first)
                    Nicotine.NONE -> add("life:nicotine-none", first)
                    else -> {}
                }
                if ((ctx.alcoholDrinks ?: 0) >= 2) add("life:alcohol", first)
                if ((ctx.caffeineCups ?: 0) >= 3) add("life:caffeine-high", first)
                if ((ctx.exercise ?: 0) >= 3) add("life:exercise-hard", first)
                if (ctx.period == true) add("life:period", first)
            }
            out += DayRow(day, feats, firstExp, badEvent != null, badEvent?.time, badEvent?.let { badBm.describe(it) }, first)
        }
        return out
    }

    // ------------------------------------------------------------------ statistics

    private class FeatureStat(
        val key: String, val exposed: Int, val bad: Int, val unexposed: Int, val badUnexposed: Int,
        val probHarm: Double, val ratio: Double, val ratioUpper: Double, val exposedRate: Double, val baselineRate: Double,
        val days: Set<Long>,
    ) {
        var p: Double = 1.0 - probHarm
        var bh: Double = 1.0
    }

    private fun gamma(rng: SplittableRandom, shape: Double): Double {
        if (shape < 1.0) return gamma(rng, shape + 1.0) * rng.nextDouble().pow(1.0 / shape)
        val d = shape - 1.0 / 3.0
        val c = 1.0 / sqrt(9.0 * d)
        while (true) {
            var x: Double
            var v: Double
            do {
                x = normal(rng)
                v = 1.0 + c * x
            } while (v <= 0)
            v = v * v * v
            val u = rng.nextDouble()
            if (u < 1 - 0.0331 * x * x * x * x) return d * v
            if (ln(u) < 0.5 * x * x + d * (1 - v + ln(v))) return d * v
        }
    }

    private fun normal(rng: SplittableRandom): Double {
        val u1 = rng.nextDouble().coerceAtLeast(1e-12)
        val u2 = rng.nextDouble()
        return sqrt(-2.0 * ln(u1)) * kotlin.math.cos(2 * Math.PI * u2)
    }

    private fun beta(rng: SplittableRandom, a: Double, b: Double): Double {
        val x = gamma(rng, a)
        val y = gamma(rng, b)
        return x / (x + y)
    }

    private fun compare(key: String, e: Int, ke: Int, u: Int, ku: Int, p0: Double, days: Set<Long>): FeatureStat {
        val a0 = max(0.5, PRIOR_STRENGTH * p0)
        val b0 = max(0.5, PRIOR_STRENGTH * (1 - p0))
        val rng = SplittableRandom(key.hashCode().toLong() * 31 + e * 7 + ke)
        var harm = 0
        val ratios = DoubleArray(DRAWS)
        for (i in 0 until DRAWS) {
            val te = beta(rng, a0 + ke, b0 + e - ke)
            val tu = beta(rng, a0 + ku, b0 + u - ku)
            if (te > tu) harm++
            ratios[i] = te / max(tu, 1e-6)
        }
        ratios.sort()
        val meanE = (a0 + ke) / (a0 + b0 + e)
        val meanU = (a0 + ku) / (a0 + b0 + u)
        return FeatureStat(key, e, ke, u, ku, harm.toDouble() / DRAWS, meanE / max(meanU, 1e-6), ratios[(DRAWS * 0.9).toInt()],
            if (e > 0) ke.toDouble() / e else 0.0, if (u > 0) ku.toDouble() / u else 0.0, days)
    }

    private fun jaccard(a: Set<Long>, b: Set<Long>): Double {
        if (a.isEmpty() && b.isEmpty()) return 0.0
        val inter = a.count { it in b }
        return inter.toDouble() / (a.size + b.size - inter)
    }

    // ------------------------------------------------------------------ report

    fun analyze(diary: Diary, now: Long): Report = analyzeInternal(diary, now, withStability = true)

    private fun analyzeInternal(diary: Diary, now: Long, withStability: Boolean): Report {
        val rows = dayRows(diary, now)
        val n = rows.size
        val badDays = rows.count { it.bad }
        val p0 = if (n == 0) 0.2 else ((badDays + 1.0) / (n + 2.0))
        val allFeatures = rows.flatMap { it.features }.toSet()
        val stats = mutableListOf<FeatureStat>()
        for (f in allFeatures) {
            val exposedRows = rows.filter { f in it.features }
            val e = exposedRows.size
            if (e < 1 || n - e < 1) continue
            val ke = exposedRows.count { it.bad }
            stats += compare(f, e, ke, n - e, badDays - ke, p0, exposedRows.map { it.day }.toSet())
        }
        // Benjamini-Hochberg over everything with at least 3 days, one-sided.
        val testable = stats.filter { it.exposed >= 3 }.sortedBy { it.p }
        val m = testable.size.coerceAtLeast(1)
        var minSoFar = 1.0
        for (i in testable.indices.reversed()) {
            minSoFar = min(minSoFar, testable[i].p * m / (i + 1))
            testable[i].bh = minSoFar
        }
        fun tier(s: FeatureStat): Tier = when {
            s.exposed >= 6 && s.bh <= 0.10 && s.ratio >= 1.6 && s.probHarm >= 0.95 -> Tier.STRONG
            s.exposed >= 4 && s.bh <= 0.25 && s.ratio >= 1.4 && s.probHarm >= 0.9 -> Tier.MODERATE
            s.exposed >= 3 && s.probHarm >= 0.88 && s.ratio >= 1.3 -> Tier.WEAK
            s.exposed >= 2 && s.probHarm >= 0.78 && s.ratio >= 1.2 && s.bad >= 2 -> Tier.EARLY
            else -> Tier.NONE
        }
        val byKey = stats.associateBy { it.key }
        fun strength(s: FeatureStat) = ln(max(s.ratio, 1.0001)) * sqrt(s.exposed.toDouble()) * s.probHarm
        val candidates = stats.filter { tier(it) != Tier.NONE }
            .sortedWith(compareByDescending<FeatureStat> { tier(it).rank }.thenByDescending { strength(it) })
        // When two suspects overlap, keep the one whose effect survives without the other:
        // pizza is "explained" by garlic if garlic days without pizza are still bad and pizza
        // days are almost all garlic days.
        val explainedBy = mutableMapOf<String, String>()
        fun rate(list: List<DayRow>) = if (list.isEmpty()) 0.0 else list.count { it.bad }.toDouble() / list.size
        for (i in candidates.indices) for (j in candidates.indices) {
            if (i == j) continue
            val a = candidates[i]; val b = candidates[j]
            if (a.key in explainedBy || b.key in explainedBy) continue
            if (kindOf(a.key) == FeatureKind.LIFESTYLE || kindOf(b.key) == FeatureKind.LIFESTYLE) continue
            if (jaccard(a.days, b.days) < 0.2) continue
            val aOnly = rows.filter { a.key in it.features && b.key !in it.features }
            val bOnly = rows.filter { b.key in it.features && a.key !in it.features }
            val neither = rows.filter { a.key !in it.features && b.key !in it.features }
            if (aOnly.size < 3 || neither.size < 3) continue
            val effA = rate(aOnly) - rate(neither)
            val effB = if (bOnly.size >= 3) rate(bOnly) - rate(neither) else null
            if (effA >= 0.15 && (effB == null || effB < 0.05)) explainedBy[b.key] = a.key
        }
        val ranked = candidates.filter { it.key !in explainedBy }

        // Merge features that are really the same thing (same days), report tangles.
        val consumed = mutableSetOf<String>()
        val suspects = mutableListOf<Suspect>()
        val lifestyle = mutableListOf<Suspect>()
        for (s in ranked) {
            if (s.key in consumed) continue
            consumed += s.key
            val same = stats.filter { it.key != s.key && it.key !in consumed && jaccard(it.days, s.days) >= 0.85 && kindOf(it.key) != FeatureKind.LIFESTYLE }
            consumed += same.map { it.key }
            val tangled = ranked.filter { it.key != s.key && it.key !in consumed && kindOf(it.key) != FeatureKind.LIFESTYLE && jaccard(it.days, s.days) in 0.5..0.85 }
            val separation = tangled.firstOrNull()?.let { t -> separate(rows, s.key, t.key) }
            val lags = rows.filter { s.key in it.features && it.bad && it.badTime != null }.mapNotNull { r ->
                r.firstExposure[s.key]?.let { (r.badTime!! - it).toDouble() / HOUR }?.takeIf { it > 0 }
            }.sorted()
            val evidence = rows.filter { s.key in it.features }.sortedByDescending { it.day }.take(14).map { r ->
                val eaten = diary.meals.filter { diary.dayOf(it.time) == r.day && (s.key in it.keys || s.key.startsWith("avoid:") && s.key.removePrefix("avoid:") in it.avoidHits) }.flatMap { it.names }.distinct().take(4)
                val lag = r.firstExposure[s.key]?.let { t -> r.badTime?.let { (it - t).toDouble() / HOUR } }
                EvidenceDay(r.day, eaten, r.bad, if (r.bad) "${r.badDesc}${lag?.let { " ${it.toInt()}h later" } ?: ""}" else "no bad BM after", lag)
            }
            val sus = Suspect(
                key = s.key, label = label(s.key), kind = kindOf(s.key), tier = tier(s), probHarm = s.probHarm, ratio = s.ratio,
                exposedDays = s.exposed, badAfter = s.bad, exposedRate = s.exposedRate, baselineRate = s.baselineRate,
                typicalLagHours = lags.takeIf { it.isNotEmpty() }?.let { it[it.size / 2] },
                sameAs = same.map { label(it.key) }.distinct().take(5),
                includes = explainedBy.filterValues { it == s.key }.keys.map { label(it) }.take(6),
                tangledWith = tangled.map { label(it.key) }.take(3),
                separation = separation, evidence = evidence,
            )
            if (sus.kind == FeatureKind.LIFESTYLE) lifestyle += sus else suspects += sus
        }

        val safe = stats.filter { kindOf(it.key) == FeatureKind.FOOD && it.exposed >= 4 && it.probHarm <= 0.35 && it.exposedRate <= max(it.baselineRate, 0.0) + 0.02 && it.ratioUpper <= 1.6 }
            .sortedByDescending { it.exposed }.take(20).map { SafeFood(it.key, label(it.key), it.exposed, it.bad) }

        val symptomSuspects = symptomAnalysis(diary, now)
        val quality = quality(diary, now, rows, if (withStability) suspects else null)
        val nextTest = suspects.firstOrNull { it.tangledWith.isNotEmpty() && it.tier.rank >= Tier.WEAK.rank }?.let {
            NextTest(it.key, it.label, "${it.label} keeps showing up with ${it.tangledWith.first()}, so Pickless can't tell which one it is. Test ${it.label.lowercase()} on its own.")
        } ?: suspects.firstOrNull { it.tier == Tier.WEAK || it.tier == Tier.MODERATE }?.let {
            NextTest(it.key, it.label, "A planned 3-day test would confirm whether ${it.label.lowercase()} is really a trigger.")
        }
        return Report(now, suspects.take(15), lifestyle, symptomSuspects, safe, quality, if (n == 0) 0.0 else badDays.toDouble() / n, n,
            nextTest, bmPattern(diary), byKey.mapValues { (_, s) -> Report.Stat(s.probHarm, s.ratio, s.exposed, s.bad) })
    }

    private fun separate(rows: List<DayRow>, a: String, b: String): String? {
        val aOnly = rows.filter { a in it.features && b !in it.features }
        val bOnly = rows.filter { b in it.features && a !in it.features }
        if (aOnly.size < 2 && bOnly.size < 2) return null
        fun part(label: String, list: List<DayRow>) = if (list.isEmpty()) null else "$label alone: ${list.count { it.bad }} bad out of ${list.size} days"
        return listOfNotNull(part(label(a), aOnly), part(label(b), bOnly)).joinToString("; ")
    }

    /** Meal-level: bloating, pain, gas or cramps (severity 2+) within 8 hours. */
    private fun symptomAnalysis(diary: Diary, now: Long): List<Suspect> {
        val significant = diary.symptoms.filter { it.severity >= 2 }
        if (significant.size < 3) return emptyList()
        val meals = diary.meals.filter { it.time + 8 * HOUR <= now && diary.dayMap[diary.dayOf(it.time)]?.let { d -> d.sick || d.travel } != true }
        if (meals.size < 6) return emptyList()
        val outcome = meals.associate { m -> m.id to significant.any { it.time in (m.time + HOUR / 2)..(m.time + 8 * HOUR) } }
        val n = meals.size
        val bad = outcome.values.count { it }
        val p0 = (bad + 1.0) / (n + 2.0)
        val feats = meals.flatMap { m -> m.keys + m.fodmapUnits.filter { it.value >= 1.0 }.keys.map { "fodmap:${it.id}" } }.toSet()
        val out = mutableListOf<Suspect>()
        for (f in feats) {
            val ex = meals.filter { f in it.keys || (f.startsWith("fodmap:") && (it.fodmapUnits[FodmapGroup.byId(f.removePrefix("fodmap:"))] ?: 0.0) >= 1.0) }
            if (ex.size < 3 || n - ex.size < 2) continue
            val ke = ex.count { outcome[it.id] == true }
            val s = compare("sym:$f", ex.size, ke, n - ex.size, bad - ke, p0, ex.map { it.id }.toSet())
            val tier = when {
                s.exposed >= 6 && s.probHarm >= 0.97 && s.ratio >= 1.6 -> Tier.MODERATE
                s.exposed >= 3 && s.probHarm >= 0.9 && s.ratio >= 1.3 -> Tier.WEAK
                else -> Tier.NONE
            }
            if (tier == Tier.NONE) continue
            out += Suspect(
                key = f, label = label(f), kind = kindOf(f), tier = tier, probHarm = s.probHarm, ratio = s.ratio, exposedDays = s.exposed,
                badAfter = s.bad, exposedRate = s.exposedRate, baselineRate = s.baselineRate, typicalLagHours = null, sameAs = emptyList(),
                tangledWith = emptyList(), separation = null, evidence = emptyList(), outcome = "bloating, pain or gas within 8 hours",
            )
        }
        return out.sortedByDescending { it.probHarm }.take(8)
    }

    private fun quality(diary: Diary, now: Long, rows: List<DayRow>, suspects: List<Suspect>?): DataQuality {
        val loggedDays = diary.meals.map { diary.dayOf(it.time) }.toSet()
        val checkins = diary.days.filter { it.checkedIn }.map { it.epochDay }.toSet()
        val bmDays = diary.bms.map { diary.dayOf(it.time) }.toSet()
        val daysLogged = loggedDays.count { it in checkins || it in bmDays }
        val today = diary.dayOf(now)
        val first = loggedDays.minOrNull()
        val window = if (first == null) emptyList() else ((max(first, today - 21))..(today - 1)).toList()
        val complete = window.count { d -> diary.meals.count { diary.dayOf(it.time) == d } >= 2 && (d in checkins || d in bmDays) }
        val completeness = if (window.isEmpty()) 0.0 else complete.toDouble() / window.size
        val rated = diary.bms.size
        val bad = diary.bms.count { badBm.isBad(it) }
        val stable = if (suspects == null) true else {
            val before = analyzeInternal(diary.until(now - 7 * DAY), now - 7 * DAY, withStability = false)
            // Stable when the current top suspect was already near the top a week ago (or there is none either time).
            val top = suspects.firstOrNull { it.tier.rank >= Tier.WEAK.rank }?.key
            val earlier = before.suspects.filter { it.tier.rank >= Tier.EARLY.rank }.take(3).map { it.key }
            if (top == null) before.suspects.none { it.tier.rank >= Tier.MODERATE.rank } else top in earlier
        }
        val missing = mutableListOf<String>()
        if (daysLogged < 14) missing += "log ${14 - daysLogged} more day${if (14 - daysLogged == 1) "" else "s"} (meals plus a check-in or BM)"
        if (completeness < 0.7 && window.size >= 7) missing += "log at least 2 meals and a check-in on most days"
        if (rated < 20) missing += "rate ${20 - rated} more BM${if (20 - rated == 1) "" else "s"}"
        if (bad < 4) missing += "a few more bad days to compare against (good news, really)"
        if (!stable && missing.isEmpty()) missing += "a week where the top suspects stay the same"
        return DataQuality(daysLogged, completeness, rated, bad, stable, missing)
    }

    fun bmPattern(diary: Diary): BmPattern {
        val bms = diary.bms
        val days = bms.map { diary.dayOf(it.time) }.toSet().let { s -> if (s.isEmpty()) 0 else (s.max() - s.min() + 1).toInt() }
        val byType = (1..7).associateWith { t -> bms.count { it.bristol == t } }
        val n = bms.size.coerceAtLeast(1)
        val hard = bms.count { it.bristol <= 2 }.toDouble() / n
        val loose = bms.count { it.bristol >= 6 }.toDouble() / n
        val pattern = when {
            bms.size < 10 -> "Not enough BMs logged yet"
            loose >= 0.25 && hard < 0.25 -> "Mostly loose (the pattern doctors call IBS-D)"
            hard >= 0.25 && loose < 0.25 -> "Mostly hard (the pattern doctors call IBS-C)"
            hard >= 0.25 && loose >= 0.25 -> "Both loose and hard (the pattern doctors call IBS-M)"
            else -> "Mostly normal form"
        }
        return BmPattern(bms.size, days, if (days == 0) 0.0 else bms.size.toDouble() / days, byType, hard, loose,
            bms.count { it.urgency >= 2 }.toDouble() / n, bms.count { it.accident }, pattern)
    }

    // ------------------------------------------------------------------ "why was that one bad?"

    fun explain(diary: Diary, bm: BmLog, report: Report?): Explanation {
        val bad = badBm.isBad(bm)
        val since = bm.time - 72 * HOUR
        val meals = diary.meals.filter { it.time in since until (bm.time - HOUR / 2) }
        val previousBad = diary.bms.filter { it.time < bm.time && badBm.isBad(it) }.sortedByDescending { it.time }.take(10)
        val candidates = mutableMapOf<String, Culprit>()
        fun history(key: String): String? {
            if (previousBad.isEmpty()) return null
            val hits = previousBad.count { pb -> diary.meals.any { it.time in (pb.time - 36 * HOUR) until pb.time && (key in it.keys || featureInMeal(key, it)) } }
            return "came before $hits of your last ${previousBad.size} bad BMs"
        }
        fun timing(h: Double): Double = exp(-(ln(max(h, 0.5) / 14.0).pow(2)) / (2 * 0.85 * 0.85))
        for (m in meals) {
            val h = (bm.time - m.time).toDouble() / HOUR
            val t = timing(h)
            val keys = m.keys.filter { !it.startsWith("ing:") || m.keys.none { k -> k.startsWith("tag:") && labeler(k) == labeler(it) } }
            val consider = mutableListOf<Triple<String, Double, String>>()
            for (k in keys) {
                val stat = report?.statOf(k)
                val prior = when {
                    stat != null && stat.exposed >= 2 -> 0.15 + 0.75 * ((stat.probHarm - 0.5).coerceAtLeast(0.0) * 2)
                    k == "tag:fried" || k == "tag:spicy" -> 0.45
                    k == "tag:fried-light" || k == "tag:spicy-mild" -> 0.25
                    k.startsWith("tag:") -> 0.12
                    else -> 0.08
                }
                val why = when {
                    stat != null && stat.exposed >= 3 && stat.probHarm >= 0.8 -> "a suspect: bad BMs ${"%.1f".format(stat.ratio)}× more likely after it"
                    k == "tag:fried" -> "fried food"
                    k == "tag:spicy" -> "spicy food"
                    else -> "eaten ${h.toInt()}h before"
                }
                consider += Triple(k, prior, why)
            }
            m.avoidHits.forEach { a -> consider += Triple("avoid:$a", 0.55, "on your avoid list") }
            for ((g, u) in m.fodmapUnits) if (u >= 1.0) {
                val dose = min(1.0, 0.5 + u / 4)
                consider += Triple("fodmap:${g.id}", 0.5 * dose + 0.1, "high in ${g.label.lowercase()} (${"%.1f".format(u)}× the low-FODMAP amount)")
            }
            if (m.grams >= 800 || m.names.size >= 5) consider += Triple("pattern:big-meal", 0.3, "a big meal")
            if (diary.hourOf(m.time) >= 21) consider += Triple("pattern:late-meal", 0.2, "eaten late")
            for ((k, prior, why) in consider) {
                val score = prior * t
                val prev = candidates[k]
                if (prev == null || prev.score < score) {
                    val what = m.names.take(3).joinToString().ifEmpty { "a meal" }
                    candidates[k] = Culprit(k, label(k), score, "$why · in $what", h, history(k))
                }
            }
        }
        val life = mutableListOf<Culprit>()
        val d = diary.dayOf(bm.time)
        listOf(diary.dayMap[d], diary.dayMap[d - 1]).filterNotNull().forEach { c ->
            if ((c.sleepQuality ?: 4) <= 1 || (c.sleepHours ?: 8.0) < 6) life += Culprit("life:poor-sleep", label("life:poor-sleep"), 0.3, "short or poor sleep", null, null)
            if ((c.stress ?: 0) >= 3) life += Culprit("life:stress", label("life:stress"), 0.3, "a stressful day", null, null)
            if (c.nicotine == Nicotine.MORE) life += Culprit("life:nicotine-more", label("life:nicotine-more"), 0.3, "nicotine speeds up the gut", null, null)
            if ((c.alcoholDrinks ?: 0) >= 2) life += Culprit("life:alcohol", label("life:alcohol"), 0.3, "${c.alcoholDrinks} drinks", null, null)
            if (c.period == true) life += Culprit("life:period", label("life:period"), 0.25, "periods often loosen stools", null, null)
        }
        diary.events.filter { it.type == "coffee" && it.time in (bm.time - 6 * HOUR) until bm.time }.takeIf { it.isNotEmpty() }?.let {
            life += Culprit("tag:caffeine", "Coffee", 0.3, "coffee within 6 hours", (bm.time - it.last().time).toDouble() / HOUR, null)
        }
        val culprits = candidates.values.sortedByDescending { it.score }.distinctBy { it.label }.take(5)
        val summary = when {
            !bad -> "This one wasn't a bad BM, so there's nothing to explain."
            culprits.isEmpty() && life.isEmpty() -> "Nothing logged in the 3 days before stands out. Missed meals can hide the real cause."
            culprits.isEmpty() -> "No food stands out; ${life.first().reason} might be why."
            else -> "Most likely: ${culprits.first().label.lowercase()}${culprits.getOrNull(1)?.let { " or ${it.label.lowercase()}" } ?: ""}."
        }
        return Explanation(bm, bad, culprits, life.distinctBy { it.key }, summary)
    }

    private fun featureInMeal(key: String, m: MealLog): Boolean = when {
        key.startsWith("avoid:") -> key.removePrefix("avoid:") in m.avoidHits
        key.startsWith("fodmap:") -> (m.fodmapUnits[FodmapGroup.byId(key.removePrefix("fodmap:"))] ?: 0.0) >= 1.0
        else -> key in m.keys
    }

    @Suppress("unused")
    private fun near(a: Double, b: Double) = abs(a - b) < 1e-9
}
