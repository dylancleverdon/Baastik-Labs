package com.baastiklabs.pickless.data

import com.baastiklabs.pickless.core.analysis.Analysis
import com.baastiklabs.pickless.core.analysis.Analyzer
import com.baastiklabs.pickless.core.analysis.Confidence
import com.baastiklabs.pickless.core.analysis.Features
import com.baastiklabs.pickless.core.analysis.FodmapCorrection
import com.baastiklabs.pickless.core.analysis.FodmapSource
import com.baastiklabs.pickless.core.analysis.HitLevel
import com.baastiklabs.pickless.core.analysis.Verdict
import com.baastiklabs.pickless.core.detective.BmLog
import com.baastiklabs.pickless.core.detective.DayLog
import com.baastiklabs.pickless.core.detective.Diary
import com.baastiklabs.pickless.core.detective.MealLog
import com.baastiklabs.pickless.core.detective.Nicotine
import com.baastiklabs.pickless.core.detective.QuickEvent
import com.baastiklabs.pickless.core.detective.SymptomLog
import com.baastiklabs.pickless.core.fodmap.FodmapGroup
import com.baastiklabs.pickless.core.fodmap.FodmapLevel
import com.baastiklabs.pickless.core.fodmap.FodmapLimit
import com.baastiklabs.pickless.core.fodmap.FodmapProfile
import com.baastiklabs.pickless.core.fodmap.FodmapScore
import com.baastiklabs.pickless.core.kb.Categories
import com.baastiklabs.pickless.core.kb.KnowledgeBase
import com.baastiklabs.pickless.core.kb.Portion
import com.baastiklabs.pickless.net.Products
import com.baastiklabs.pickless.net.WebLookup
import com.baastiklabs.pickless.net.WebResult
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId

@Serializable
data class CustomItem(val ref: String, val name: String, val grams: Double)

@Serializable
data class StoredLimit(val group: String, val low: Double, val moderate: Double)

/** Friendly names for knowledge-base tags (the detective's "families"). */
object Tags {
    private val labels = mapOf(
        "dairy" to "Dairy", "maybe-dairy" to "Might contain dairy", "oat" to "Oats", "soy" to "Soy", "soy-refined" to "Refined soy (lecithin, soybean oil)",
        "maybe-soy" to "Unnamed vegetable oil or lecithin", "fried" to "Fried food", "fried-light" to "Pan-fried food", "maybe-fried" to "Probably fried snacks",
        "spicy" to "Spicy food", "spicy-mild" to "Mildly spicy food", "brussels-sprouts" to "Brussels sprouts", "cruciferous" to "Cabbage family",
        "wheat" to "Wheat", "gluten" to "Gluten", "garlic" to "Garlic", "onion" to "Onion", "allium" to "Garlic & onion family", "legume" to "Beans & lentils",
        "nightshade" to "Nightshades", "tree-nut" to "Tree nuts", "peanut" to "Peanuts", "egg" to "Eggs", "fish" to "Fish", "shellfish" to "Shellfish",
        "sesame" to "Sesame", "corn" to "Corn", "citrus" to "Citrus", "mushroom" to "Mushrooms", "polyol" to "Sugar alcohols", "added-sugar" to "Added sugar",
        "fiber-additive" to "Added fiber (inulin etc.)", "caffeine" to "Caffeine", "alcohol" to "Alcohol",
    )
    /** Tags that would only add noise as detective features. */
    val ignored = setOf("whole", "maybe-hidden")

    fun label(tag: String): String = labels[tag] ?: tag.replace('-', ' ').replaceFirstChar { it.uppercase() }
}

/**
 * The app's single source of truth for analysis and logging: wraps the knowledge base, analyzer,
 * products and database, and turns the database into a core [Diary] for the detective.
 */
class Repository(
    val kb: KnowledgeBase,
    val db: PicklessDb,
    val settings: Settings,
    val products: Products,
    val web: WebLookup,
) {
    val analyzer = Analyzer(kb)
    private val json = Json { ignoreUnknownKeys = true; encodeDefaults = true }
    val zone: ZoneId get() = ZoneId.systemDefault()

    suspend fun context(): Analyzer.Context {
        val p = settings.current()
        val corrections = db.corrections().allOnce().associate { c ->
            c.key to FodmapCorrection(decodeLimits(c.limitsJson), runCatching { FodmapSource.valueOf(c.source) }.getOrDefault(FodmapSource.USER), c.note)
        }
        return Analyzer.Context(p.avoid, p.tolerance, corrections)
    }

    fun decodeLimits(s: String): List<FodmapLimit> = runCatching {
        json.decodeFromString<List<StoredLimit>>(s).mapNotNull { l -> FodmapGroup.byId(l.group)?.let { FodmapLimit(it, l.low, l.moderate) } }
    }.getOrDefault(emptyList())

    fun encodeLimits(limits: List<FodmapLimit>): String = json.encodeToString(limits.map { StoredLimit(it.group.id, it.lowGrams, it.moderateGrams) })

    /** Analysis for anything a log or a search result can point at. */
    suspend fun analyze(ref: String): Analysis? = withContext(Dispatchers.Default) {
        val ctx = context()
        val (kind, id) = ref.split(':', limit = 2).let { it[0] to it.getOrElse(1) { "" } }
        when (kind) {
            "food" -> kb[id]?.let { analyzer.analyzeEntry(it, ctx) }
            "product" -> db.products().get(id)?.let { analyzer.analyzeProduct(products.decode(it), ctx) }
            "custom" -> id.toLongOrNull()?.let { db.custom().get(it) }?.let { analyzeCustom(it, ctx) }
            "guess" -> analyzeGuess(id, ctx)
            else -> null
        }
    }

    suspend fun analyzeGuess(text: String, ctx: Analyzer.Context? = null): Analysis? {
        val c = ctx ?: context()
        val web = this.web.cached(text)
        val base = analyzer.guess(text, c)
        if (web == null || web.limits.isEmpty()) {
            if (web != null && base == null) return webOnly(text, web, c)
            return base
        }
        val profile = FodmapProfile.ofLimits("guess:$text", text, web.fodmapLimits())
        val a = base ?: return webOnly(text, web, c)
        return a.copy(profile = profile, fodmapSource = FodmapSource.WEB, notes = a.notes + "Web lookup: ${web.summary}.",
            features = a.features.copy(fodmapPerGram = profile.rates.mapKeys { it.key.id }.mapValues { it.value.unitsPerGram }))
            .let { reverdict(it) }
    }

    private fun webOnly(text: String, web: WebResult, ctx: Analyzer.Context): Analysis {
        val profile = FodmapProfile.ofLimits("guess:$text", text, web.fodmapLimits())
        val portion = Portion("serving", 100.0)
        val a = Analysis(text.trim(), "Looked up on FODMAP sites", Verdict.GOOD, emptyList(), emptyList(), profile, portion, listOf(portion),
            FodmapSource.WEB, Confidence.LOW, listOf("Web lookup: ${web.summary}."),
            Features(fodmapPerGram = profile.rates.mapKeys { it.key.id }.mapValues { it.value.unitsPerGram }), emptyList(), emptyList(), null, ctx.tolerance)
        return reverdict(a)
    }

    /** Recompute the verdict after the FODMAP profile changed. */
    fun reverdict(a: Analysis): Analysis {
        val level = FodmapScore.level(a.profile.scoreAt(a.portion.grams, a.tolerance))
        val reasons = a.verdictReasons.filterNot { it.contains("FODMAP at") }.toMutableList()
        if (level != FodmapLevel.LOW) reasons += "${level.label} FODMAP at ${Analyzer.portionText(a.portion)}"
        val verdict = when {
            a.findings.any { it.level == HitLevel.AVOID } -> Verdict.AVOID
            a.findings.isNotEmpty() || level != FodmapLevel.LOW -> Verdict.CAUTION
            else -> Verdict.GOOD
        }
        return a.copy(verdict = verdict, verdictReasons = reasons)
    }

    fun items(e: CustomFoodEntity): List<CustomItem> = runCatching { json.decodeFromString<List<CustomItem>>(e.itemsJson) }.getOrDefault(emptyList())

    fun encodeItems(items: List<CustomItem>) = json.encodeToString(items)

    /** A saved meal or recipe: the sum of its parts. */
    suspend fun analyzeCustom(e: CustomFoodEntity, ctx: Analyzer.Context): Analysis? {
        val parts = items(e).mapNotNull { item -> analyzeWith(item.ref, ctx)?.let { it to item.grams } }
        if (parts.isEmpty()) return null
        val total = parts.sumOf { it.second }.coerceAtLeast(1.0)
        val profile = FodmapProfile.combine(parts.map { (a, g) -> a.profile to g / total })
        val findings = parts.flatMap { it.first.findings }.groupBy { it.item.key }.map { (_, list) ->
            val worst = list.minBy { it.level.ordinal }
            worst.copy(evidence = list.flatMap { it.evidence }.distinctBy { it.text }.take(6))
        }
        val features = Features(
            foods = parts.flatMap { it.first.features.foods }.toSet(),
            ingredients = parts.flatMap { it.first.features.ingredients }.toSet(),
            tags = parts.flatMap { it.first.features.tags }.toSet(),
            fodmapPerGram = profile.rates.mapKeys { it.key.id }.mapValues { it.value.unitsPerGram },
        )
        val portion = Portion("serving", e.servingGrams)
        val a = Analysis(e.name, "Your recipe · ${parts.joinToString { it.first.title.lowercase() }}", Verdict.GOOD, findings.flatMap { f -> listOf("${if (f.level == HitLevel.AVOID) "Contains" else "Possibly"} ${f.item.label.lowercase()}") },
            findings, profile, portion, listOf(portion, Portion("100 g", 100.0)), FodmapSource.KNOWN, Confidence.MEDIUM,
            listOf("FODMAP adds up across everything in it (stacking)."), features, parts.flatMap { it.first.ingredients }, emptyList(), null, ctx.tolerance)
        return reverdict(a)
    }

    private suspend fun analyzeWith(ref: String, ctx: Analyzer.Context): Analysis? {
        val (kind, id) = ref.split(':', limit = 2).let { it[0] to it.getOrElse(1) { "" } }
        return when (kind) {
            "food" -> kb[id]?.let { analyzer.analyzeEntry(it, ctx) }
            "product" -> db.products().get(id)?.let { analyzer.analyzeProduct(products.decode(it), ctx) }
            "guess" -> analyzeGuess(id, ctx)
            else -> null
        }
    }

    // ------------------------------------------------------------------ logging

    suspend fun logFood(ref: String, a: Analysis, grams: Double, portionLabel: String, time: Long, meal: String, note: String? = null, challengeDose: Boolean = false): Long {
        val extraFood = when {
            ref.startsWith("product:") || ref.startsWith("custom:") -> setOf(ref)
            else -> emptySet()
        }
        val f = a.features.copy(foods = a.features.foods + extraFood, tags = a.features.tags - Tags.ignored)
        val hits = a.findings.filter { it.level == HitLevel.AVOID }.joinToString(",") { it.item.key }
        return db.food().insert(
            FoodLogEntity(time = time, meal = meal, ref = ref, name = a.title, grams = grams, portionLabel = portionLabel,
                featuresJson = json.encodeToString(Features.serializer(), f), avoidHits = hits, verdict = a.verdict.name, note = note, challengeDose = challengeDose),
        )
    }

    fun features(e: FoodLogEntity): Features = runCatching { json.decodeFromString(Features.serializer(), e.featuresJson) }.getOrDefault(Features())

    // ------------------------------------------------------------------ detective input

    suspend fun diary(): Diary = withContext(Dispatchers.IO) {
        val foods = db.food().allOnce()
        val meals = mutableListOf<MealLog>()
        var group = mutableListOf<FoodLogEntity>()
        fun flush() {
            if (group.isEmpty()) return
            val keys = mutableSetOf<String>()
            val units = mutableMapOf<FodmapGroup, Double>()
            val hits = mutableSetOf<String>()
            for (e in group) {
                val f = features(e)
                keys += f.keys()
                for ((g, perGram) in f.fodmapPerGram) FodmapGroup.byId(g)?.let { units[it] = (units[it] ?: 0.0) + perGram * e.grams }
                hits += e.avoidHits.split(',').filter { it.isNotBlank() }
            }
            meals += MealLog(group.first().id, group.first().time, keys, units, hits, group.map { it.name }, group.sumOf { it.grams })
            group = mutableListOf()
        }
        for (e in foods.sortedBy { it.time }) {
            val first = group.firstOrNull()
            if (first != null && (first.meal != e.meal || e.time - first.time > 90 * 60_000L)) flush()
            group += e
        }
        flush()
        val bms = db.bm().allOnce().map { BmLog(it.id, it.time, it.bristol, it.urgency, it.accident, it.pain, it.blood) }
        val symptoms = db.symptoms().allOnce().map { SymptomLog(it.id, it.time, it.type, it.severity) }
        val events = db.events().allOnce().map { QuickEvent(it.time, it.type, it.amount) }
        // "+ Pouch" taps on the Today screen mean more nicotine than usual that day.
        val extraPouchDays = events.filter { it.type == "nicotine" }.map { epochDay(it.time) }.toSet()
        val checkins = db.checkins().allOnce().associateBy { it.epochDay }
        val days = (checkins.keys + extraPouchDays).sorted().map { d ->
            val it = checkins[d]
            val nic = Nicotine.byId(it?.nicotine).let { n -> if (d in extraPouchDays && (n == null || n == Nicotine.USUAL)) Nicotine.MORE else n }
            if (it == null) DayLog(d, nicotine = nic, checkedIn = false)
            else DayLog(it.epochDay, it.overall, it.stress, it.sleepQuality, it.sleepHours, nic, it.alcohol, it.caffeine, it.exercise,
                it.period, it.sick, it.travel, it.noBm, checkedIn = true)
        }
        Diary(meals, bms, symptoms, days, events, zone)
    }

    /** Names for detective feature keys. */
    suspend fun labeler(): (String) -> String {
        val products = db.products().allOnce().associate { it.key to it.name }
        val custom = db.custom().allOnce().associate { it.id.toString() to it.name }
        val avoid = settings.current().avoid.associate { it.key to it.label }
        return { key ->
            val prefix = key.substringBefore(':')
            val rest = key.substringAfter(':')
            when (prefix) {
                "food" -> when {
                    rest.startsWith("product:") -> products[rest.removePrefix("product:")] ?: "A product"
                    rest.startsWith("custom:") -> custom[rest.removePrefix("custom:")] ?: "A saved meal"
                    else -> kb[rest]?.name ?: rest
                }
                "ing" -> kb[rest]?.name ?: rest
                "tag" -> Tags.label(rest)
                "avoid" -> "Slips: ${avoid[rest] ?: Categories.byId(rest.removePrefix("cat:"))?.label ?: rest}"
                else -> key
            }
        }
    }

    fun epochDay(time: Long): Long = Instant.ofEpochMilli(time).atZone(zone).toLocalDate().toEpochDay()
    fun startOf(day: Long): Long = LocalDate.ofEpochDay(day).atStartOfDay(zone).toInstant().toEpochMilli()
    fun today(): Long = LocalDate.now(zone).toEpochDay()
}
