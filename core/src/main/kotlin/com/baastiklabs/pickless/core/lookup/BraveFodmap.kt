package com.baastiklabs.pickless.core.lookup

import com.baastiklabs.pickless.core.fodmap.FodmapGroup
import com.baastiklabs.pickless.core.fodmap.FodmapLimit
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject

/**
 * Looks up a food Pickless doesn't know by searching FODMAP sites with the owner's Brave Search
 * key, then reads what the result snippets say ("low FODMAP at 1/2 cup", "high in fructans").
 * The answer is an estimate and the app shows its sources.
 */
object BraveFodmap {
    const val ENDPOINT = "https://api.search.brave.com/res/v1/web/search"
    const val KEY_HEADER = "X-Subscription-Token"

    fun url(food: String): String {
        val q = java.net.URLEncoder.encode("$food low FODMAP serving size", "UTF-8")
        return "$ENDPOINT?q=$q&count=10&extra_snippets=true&safesearch=strict&text_decorations=false"
    }

    enum class WebVerdict(val label: String) { LOW("Low FODMAP"), MODERATE("Moderate FODMAP"), HIGH("High FODMAP"), MIXED("Depends on the serving"), UNKNOWN("Couldn't tell") }

    data class Source(val title: String, val url: String, val host: String, val excerpt: String, val trust: Int)

    data class Result(
        val food: String,
        val verdict: WebVerdict,
        val groups: Set<FodmapGroup>,
        val lowServingGrams: Double?,
        val lowServingText: String?,
        val limits: List<FodmapLimit>,
        val sources: List<Source>,
    ) {
        val summary: String get() = buildString {
            append(verdict.label)
            if (lowServingText != null) append(", low up to $lowServingText")
            if (groups.isNotEmpty() && verdict != WebVerdict.LOW) append(" (").append(groups.joinToString { it.label.lowercase() }).append(")")
        }
    }

    private val trusted = mapOf(
        "monashfodmap.com" to 3, "fodmapfriendly.com" to 3,
        "fodmapeveryday.com" to 2, "alittlebityummy.com" to 2, "casadesante.com" to 2, "karlijnskitchen.com" to 2,
        "katescarlata.com" to 2, "ibsdiets.org" to 2, "rachelpaulsfood.com" to 2, "gourmendfoods.com" to 2, "fodzyme.com" to 2,
        "joyfulbelly.com" to 1, "healthline.com" to 1, "verywellhealth.com" to 1, "webmd.com" to 1, "ibsgamechanger.com" to 2,
        "theibsnetwork.org" to 2, "dietvsdisease.org" to 1, "myfodmapdiet.com" to 1, "nutritionfacts.org" to 1,
    )

    private val json = Json { ignoreUnknownKeys = true; isLenient = true }
    private val verdictRe = Regex("\\b(not\\s+)?(?:considered\\s+|rated\\s+|tested\\s+(?:as\\s+)?|is\\s+|are\\s+|as\\s+|a\\s+|very\\s+)*(low|moderate|medium|high)(?:-|\\s)?(?:in\\s+)?fodmaps?\\b")
    private val friendlyRe = Regex("\\bfodmap[- ]friendly\\b")
    private val servingRe = Regex(
        "(?:low[- ]fodmap|safe|green)\\s+(?:serving|portion|amount|serve)(?:\\s+size)?\\s*(?:is|of|:)?\\s*(?:up\\s+to\\s+|about\\s+|around\\s+)?" +
            "(\\d+(?:\\.\\d+)?|\\d+/\\d+|½|¼|¾|⅓|one|two|three|half)\\s*(cups?|tbsp|tablespoons?|tsp|teaspoons?|g|grams?|ml|slices?|pieces?|medium|small|large|whole|spears?|stalks?|cloves?)",
    )
    private val servingRe2 = Regex(
        "low[- ]fodmap\\s+(?:in|at|up\\s+to)\\s+(?:servings?\\s+of\\s+|amounts?\\s+of\\s+|a\\s+serving\\s+of\\s+)?(?:up\\s+to\\s+)?" +
            "(\\d+(?:\\.\\d+)?|\\d+/\\d+|½|¼|¾|⅓|one|two|three|half)\\s*(cups?|tbsp|tablespoons?|tsp|teaspoons?|g|grams?|ml|slices?|pieces?|medium|small|large|whole|spears?|stalks?|cloves?)",
    )
    private val groupWords = listOf(
        "fructan" to FodmapGroup.FRUCTANS, "galacto" to FodmapGroup.GOS, " gos" to FodmapGroup.GOS, "lactose" to FodmapGroup.LACTOSE,
        "fructose" to FodmapGroup.FRUCTOSE, "sorbitol" to FodmapGroup.SORBITOL, "mannitol" to FodmapGroup.MANNITOL,
    )

    fun parse(food: String, body: String, defaultServingGrams: Double = 100.0): Result {
        val root = runCatching { json.parseToJsonElement(body).jsonObject }.getOrNull()
        val results = (root?.get("web") as? JsonObject)?.get("results")?.let { runCatching { it.jsonArray }.getOrNull() } ?: emptyList()
        val foodWords = food.lowercase().split(Regex("[^a-z0-9]+")).filter { it.length >= 3 && it !in setOf("and", "with", "the", "low", "fodmap") }
        var low = 0.0; var mod = 0.0; var high = 0.0
        val groups = mutableMapOf<FodmapGroup, Double>()
        val servings = mutableListOf<Pair<Double, String>>()
        val sources = mutableListOf<Source>()
        for (r in results) {
            val o = r as? JsonObject ?: continue
            fun s(k: String) = (o[k] as? JsonPrimitive)?.contentOrNull ?: ""
            val url = s("url")
            val host = ((o["meta_url"] as? JsonObject)?.get("hostname") as? JsonPrimitive)?.contentOrNull
                ?: runCatching { java.net.URI(url).host }.getOrNull() ?: ""
            val bare = host.removePrefix("www.").lowercase()
            val trust = trusted.entries.firstOrNull { bare == it.key || bare.endsWith("." + it.key) }?.value ?: 1
            val extras = (o["extra_snippets"])?.let { e -> runCatching { e.jsonArray.mapNotNull { (it as? JsonPrimitive)?.contentOrNull } }.getOrNull() } ?: emptyList()
            val pieces = listOf(s("title"), s("description")) + extras
            var used = false
            var excerpt = ""
            for (raw in pieces) {
                val text = clean(raw)
                if (foodWords.isNotEmpty() && foodWords.none { text.contains(it) }) continue
                var found = false
                for (m in verdictRe.findAll(text)) {
                    val neg = m.groupValues[1].isNotBlank()
                    when (m.groupValues[2]) {
                        "low" -> if (neg) high += trust else low += trust
                        "moderate", "medium" -> mod += trust
                        "high" -> if (neg) low += trust * 0.5 else high += trust
                    }
                    found = true
                }
                if (friendlyRe.containsMatchIn(text)) { low += trust * 0.5; found = true }
                for (re in listOf(servingRe, servingRe2)) re.findAll(text).forEach { m ->
                    val g = grams(m.groupValues[1], m.groupValues[2], defaultServingGrams)
                    if (g != null) servings += g to "${m.groupValues[1]} ${m.groupValues[2]}"
                    found = true
                }
                if (found) {
                    used = true
                    if (excerpt.isEmpty()) excerpt = text.take(220)
                    for ((w, g) in groupWords) if (text.contains(w)) groups[g] = (groups[g] ?: 0.0) + trust
                    if (text.contains("polyol")) { groups[FodmapGroup.SORBITOL] = (groups[FodmapGroup.SORBITOL] ?: 0.0) + trust * 0.5 }
                }
            }
            if (used) sources += Source(clean(s("title")), url, bare, excerpt, trust)
        }
        val total = low + mod + high
        val verdict = when {
            total <= 0.0 -> WebVerdict.UNKNOWN
            low / total >= 0.65 && servings.isEmpty() -> WebVerdict.LOW
            low / total >= 0.65 -> WebVerdict.MIXED.takeIf { high > 0 || mod > 0 } ?: WebVerdict.LOW
            high / total >= 0.65 -> if (servings.isNotEmpty() && low > 0) WebVerdict.MIXED else WebVerdict.HIGH
            mod / total >= 0.5 -> WebVerdict.MODERATE
            else -> WebVerdict.MIXED
        }
        val serving = servings.groupingBy { it.second }.eachCount().maxByOrNull { it.value }?.key?.let { key -> servings.first { it.second == key } }
        val strongGroups = groups.filter { it.value >= 1.0 }.keys
        val gs = strongGroups.ifEmpty { setOf(FodmapGroup.OTHER) }
        val limits: List<FodmapLimit> = when (verdict) {
            WebVerdict.UNKNOWN -> emptyList()
            WebVerdict.LOW -> if (serving != null) gs.map { FodmapLimit(it, serving.first, serving.first * 2) } else emptyList()
            WebVerdict.MIXED -> { val lo = serving?.first ?: (defaultServingGrams / 2); gs.map { FodmapLimit(it, lo, lo * 2) } }
            WebVerdict.MODERATE -> { val lo = serving?.first ?: (defaultServingGrams / 2); gs.map { FodmapLimit(it, lo, maxOf(lo * 1.5, defaultServingGrams)) } }
            WebVerdict.HIGH -> { val lo = serving?.first ?: (defaultServingGrams / 4); gs.map { FodmapLimit(it, lo, lo * 2) } }
        }
        return Result(food, verdict, strongGroups, serving?.first, serving?.second, limits,
            sources.sortedByDescending { it.trust }.distinctBy { it.host }.take(4))
    }

    private fun clean(s: String) = s.replace(Regex("<[^>]+>"), "").replace("&amp;", "&").replace("&#x27;", "'").replace("&quot;", "\"")
        .replace("&nbsp;", " ").lowercase().replace(Regex("\\s+"), " ")

    private fun grams(qty: String, unit: String, defaultServing: Double): Double? {
        val n = when (qty) { "one" -> 1.0; "two" -> 2.0; "three" -> 3.0; "half" -> 0.5; else -> Units.number(qty) } ?: return null
        val u = unit.lowercase()
        val per = when {
            u.startsWith("g") || u == "ml" -> 1.0
            u.startsWith("cup") -> 150.0
            u.startsWith("tbsp") || u.startsWith("tablespoon") -> 15.0
            u.startsWith("tsp") || u.startsWith("teaspoon") -> 5.0
            u.startsWith("slice") -> 30.0
            u.startsWith("clove") -> 3.0
            u.startsWith("spear") || u.startsWith("stalk") -> 15.0
            u == "small" -> defaultServing * 0.7
            u == "large" -> defaultServing * 1.4
            else -> defaultServing
        }
        return (n * per).takeIf { it > 0 }
    }
}
