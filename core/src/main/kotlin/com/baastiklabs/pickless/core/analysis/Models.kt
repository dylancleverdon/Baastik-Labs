package com.baastiklabs.pickless.core.analysis

import com.baastiklabs.pickless.core.fodmap.FodmapGroup
import com.baastiklabs.pickless.core.fodmap.FodmapLevel
import com.baastiklabs.pickless.core.fodmap.FodmapProfile
import com.baastiklabs.pickless.core.fodmap.FodmapScore
import com.baastiklabs.pickless.core.fodmap.GroupLoad
import com.baastiklabs.pickless.core.kb.AvoidItem
import com.baastiklabs.pickless.core.kb.Portion
import kotlinx.serialization.Serializable

enum class Verdict(val label: String) { GOOD("Looks good"), CAUTION("Caution"), AVOID("Avoid"), UNKNOWN("Not sure") }

enum class HitLevel { AVOID, CAUTION, TRACE }

/** Why something matched: the words on the label (or the food name) and what they mean. */
data class Evidence(val text: String, val reason: String)

data class AvoidFinding(val item: AvoidItem, val level: HitLevel, val evidence: List<Evidence>)

enum class FodmapSource(val label: String) {
    KNOWN("Built-in estimate"),
    ESTIMATED("Estimated from the ingredients"),
    CERTIFIED("Certified low FODMAP on the label"),
    WEB("Looked up on FODMAP sites"),
    GUESS("Educated guess"),
    USER("Your correction"),
    NONE("No FODMAP information"),
}

enum class Confidence(val label: String) { HIGH("High"), MEDIUM("Medium"), LOW("Low") }

/** A product read from a database or a label photo. Everything optional: data is often incomplete. */
@Serializable
data class ProductInput(
    val code: String? = null,
    val name: String? = null,
    val brand: String? = null,
    val ingredientsText: String? = null,
    val categories: List<String> = emptyList(),
    val labels: List<String> = emptyList(),
    /** Allergen tags from the database (Open Food Facts style, e.g. "en:milk"). */
    val allergens: List<String> = emptyList(),
    val traces: List<String> = emptyList(),
    val servingGrams: Double? = null,
    val servingLabel: String? = null,
    val fatPer100: Double? = null,
    val sugarsPer100: Double? = null,
    val fiberPer100: Double? = null,
    val polyolsPer100: Double? = null,
    val source: String = "manual",
)

/** What the detective sees for something eaten (per gram; the log multiplies by the amount). */
@Serializable
data class Features(
    val foods: Set<String> = emptySet(),
    val ingredients: Set<String> = emptySet(),
    val tags: Set<String> = emptySet(),
    val fodmapPerGram: Map<String, Double> = emptyMap(),
) {
    fun keys(): Set<String> = buildSet {
        foods.forEach { add("food:$it") }
        ingredients.forEach { add("ing:$it") }
        tags.forEach { add("tag:$it") }
    }
}

/** One ingredient of a label as the app shows it: the label's words and what Pickless took them to be. */
data class MatchedIngredient(
    val labelText: String,
    val entryId: String?,
    val entryName: String?,
    val avoidLabels: List<String>,
    val cautionLabels: List<String>,
    val fodmapGroups: List<FodmapGroup>,
    val depth: Int,
    val minor: Boolean,
)

data class FodmapAt(
    val grams: Double,
    val loads: List<GroupLoad>,
    val score: Double,
    val personalScore: Double?,
) {
    val level: FodmapLevel get() = FodmapScore.level(personalScore ?: score)
    val standardLevel: FodmapLevel get() = FodmapScore.level(score)
}

data class Analysis(
    val title: String,
    val subtitle: String?,
    val verdict: Verdict,
    val verdictReasons: List<String>,
    val findings: List<AvoidFinding>,
    val profile: FodmapProfile,
    val portion: Portion,
    val portions: List<Portion>,
    val fodmapSource: FodmapSource,
    val confidence: Confidence,
    val notes: List<String>,
    val features: Features,
    val ingredients: List<MatchedIngredient>,
    val unknownIngredients: List<String>,
    val identityId: String?,
    val tolerance: Map<FodmapGroup, Double>,
) {
    fun fodmapAt(grams: Double = portion.grams): FodmapAt {
        val loads = profile.loadsAt(grams)
        val personal = if (tolerance.isEmpty() || tolerance.values.all { it == 1.0 }) null else profile.scoreAt(grams, tolerance)
        return FodmapAt(grams, loads, FodmapScore.overall(loads), personal)
    }

    val avoidHits: List<AvoidFinding> get() = findings.filter { it.level == HitLevel.AVOID }
    val cautionHits: List<AvoidFinding> get() = findings.filter { it.level != HitLevel.AVOID }
}
