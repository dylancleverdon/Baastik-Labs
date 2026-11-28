package com.baastiklabs.pickless.core.fodmap

import kotlin.math.exp
import kotlin.math.max
import kotlin.math.min

/** The six FODMAP types. IDs are stored in logs: never rename. */
enum class FodmapGroup(val id: String, val label: String, val plain: String) {
    FRUCTANS("fructans", "Fructans", "found in wheat, garlic, onion"),
    GOS("gos", "GOS", "found in beans, lentils, whole soybeans"),
    LACTOSE("lactose", "Lactose", "the sugar in milk"),
    FRUCTOSE("fructose", "Excess fructose", "found in honey, apples, high-fructose corn syrup"),
    SORBITOL("sorbitol", "Sorbitol", "a sugar alcohol in stone fruit and sugar-free sweets"),
    MANNITOL("mannitol", "Mannitol", "a sugar alcohol in mushrooms and cauliflower"),
    OTHER("other", "FODMAPs (type not stated)", "a source said FODMAP without saying which kind");

    companion object {
        fun byId(id: String): FodmapGroup? = entries.firstOrNull { it.id == id }
    }
}

/** Up to [lowGrams] of the food is low (green), up to [moderateGrams] moderate (amber), beyond that high. */
data class FodmapLimit(val group: FodmapGroup, val lowGrams: Double, val moderateGrams: Double) {
    init {
        require(lowGrams > 0 && moderateGrams >= lowGrams) { "bad limit for $group: $lowGrams/$moderateGrams" }
    }
}

enum class FodmapLevel(val label: String) { LOW("Low"), MODERATE("Moderate"), HIGH("High") }

/**
 * One FODMAP type's load for an amount of food, measured in "low servings": 1.0 means exactly the
 * largest amount still rated low. [moderateAt] is where the moderate band ends, in the same units.
 */
data class GroupLoad(val group: FodmapGroup, val units: Double, val moderateAt: Double) {
    val score: Double get() = FodmapScore.groupScore(units, moderateAt)
    val level: FodmapLevel get() = FodmapScore.level(score)
}

object FodmapScore {
    /** 0-10 for one group: 2.5 at the edge of "low", 6 at the edge of "moderate", approaching 10 beyond. */
    fun groupScore(units: Double, moderateAt: Double): Double {
        if (units <= 0) return 0.0
        val m = max(moderateAt, 1.0)
        return when {
            units <= 1.0 -> 2.5 * units
            units <= m -> if (m - 1.0 < 1e-6) 6.0 else 2.5 + 3.5 * (units - 1.0) / (m - 1.0)
            else -> 6.0 + 4.0 * (1.0 - exp(-(units - m) / m))
        }
    }

    /** Overall score: the worst group, plus a little for every other group that is at least moderate. */
    fun overall(loads: Collection<GroupLoad>): Double {
        if (loads.isEmpty()) return 0.0
        val scores = loads.map { it.score }.sortedDescending()
        val extra = scores.drop(1).count { it >= 3.0 } * 0.5
        return min(10.0, scores.first() + extra)
    }

    fun level(score: Double): FodmapLevel = when {
        score < 3.0 -> FodmapLevel.LOW
        score < 6.0 -> FodmapLevel.MODERATE
        else -> FodmapLevel.HIGH
    }
}

/**
 * How FODMAP load grows with the amount eaten: for each group, units per gram and where the
 * moderate band ends. Linear in grams, so a serving slider can rescale it for any portion, and
 * profiles add up (a meal is the sum of its foods: "FODMAP stacking").
 */
data class FodmapProfile(val rates: Map<FodmapGroup, Rate> = emptyMap(), val drivers: Map<FodmapGroup, List<Driver>> = emptyMap()) {
    /** [unitsPerGram] of the group per gram of food; [moderateAt] in units (>= 1). */
    data class Rate(val unitsPerGram: Double, val moderateAt: Double)

    /** Which food or ingredient contributes to a group, and how much (units per gram of the whole). */
    data class Driver(val entryId: String, val name: String, val unitsPerGram: Double)

    val isEmpty: Boolean get() = rates.values.all { it.unitsPerGram <= 0 }

    fun loadsAt(grams: Double, tolerance: Map<FodmapGroup, Double> = emptyMap()): List<GroupLoad> =
        rates.mapNotNull { (g, r) ->
            val units = r.unitsPerGram * grams * (tolerance[g] ?: 1.0)
            if (units <= 1e-9) null else GroupLoad(g, units, r.moderateAt)
        }.sortedByDescending { it.score }

    fun scoreAt(grams: Double, tolerance: Map<FodmapGroup, Double> = emptyMap()): Double = FodmapScore.overall(loadsAt(grams, tolerance))

    /** Largest amount that keeps every group low, or null when nothing limits it. */
    fun safeGrams(tolerance: Map<FodmapGroup, Double> = emptyMap()): Double? {
        val worst = rates.entries.maxOfOrNull { (g, r) -> r.unitsPerGram * (tolerance[g] ?: 1.0) } ?: return null
        return if (worst <= 1e-9) null else 1.0 / worst
    }

    fun scaled(factor: Double): FodmapProfile = FodmapProfile(
        rates.mapValues { (_, r) -> r.copy(unitsPerGram = r.unitsPerGram * factor) },
        drivers.mapValues { (_, list) -> list.map { it.copy(unitsPerGram = it.unitsPerGram * factor) } },
    )

    operator fun plus(other: FodmapProfile): FodmapProfile = combine(listOf(this to 1.0, other to 1.0))

    companion object {
        val NONE = FodmapProfile()

        fun ofLimits(entryId: String, name: String, limits: List<FodmapLimit>): FodmapProfile = FodmapProfile(
            limits.associate { it.group to Rate(1.0 / it.lowGrams, it.moderateGrams / it.lowGrams) },
            limits.associate { it.group to listOf(Driver(entryId, name, 1.0 / it.lowGrams)) },
        )

        /**
         * Weighted sum of profiles (weight = grams of that part per gram of the whole). The moderate
         * edge of the sum is the load-weighted average of the parts' edges.
         */
        fun combine(parts: List<Pair<FodmapProfile, Double>>): FodmapProfile {
            val rates = mutableMapOf<FodmapGroup, Rate>()
            val drivers = mutableMapOf<FodmapGroup, MutableList<Driver>>()
            for (g in FodmapGroup.entries) {
                var units = 0.0
                var weightedEdge = 0.0
                for ((p, w) in parts) {
                    val r = p.rates[g] ?: continue
                    val u = r.unitsPerGram * w
                    units += u
                    weightedEdge += u * r.moderateAt
                    p.drivers[g]?.forEach { d -> drivers.getOrPut(g) { mutableListOf() }.add(d.copy(unitsPerGram = d.unitsPerGram * w)) }
                }
                if (units > 0) rates[g] = Rate(units, weightedEdge / units)
            }
            val merged = drivers.mapValues { (_, list) ->
                list.groupBy { it.entryId }.map { (_, ds) -> ds.first().copy(unitsPerGram = ds.sumOf { it.unitsPerGram }) }
                    .sortedByDescending { it.unitsPerGram }
            }
            return FodmapProfile(rates, merged)
        }
    }
}
