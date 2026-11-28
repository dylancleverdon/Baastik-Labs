package com.baastiklabs.pickless.core.analysis

import com.baastiklabs.pickless.core.fodmap.FodmapGroup
import com.baastiklabs.pickless.core.fodmap.FodmapLevel
import com.baastiklabs.pickless.core.fodmap.FodmapLimit
import com.baastiklabs.pickless.core.fodmap.FodmapProfile
import com.baastiklabs.pickless.core.fodmap.FodmapScore
import com.baastiklabs.pickless.core.kb.AvoidItem
import com.baastiklabs.pickless.core.kb.AvoidStatus
import com.baastiklabs.pickless.core.kb.Categories
import com.baastiklabs.pickless.core.kb.EntryKind
import com.baastiklabs.pickless.core.kb.FoodEntry
import com.baastiklabs.pickless.core.kb.KnowledgeBase
import com.baastiklabs.pickless.core.kb.Portion
import com.baastiklabs.pickless.core.kb.Strictness
import com.baastiklabs.pickless.core.kb.TracePolicy
import com.baastiklabs.pickless.core.label.IngredientNode
import com.baastiklabs.pickless.core.label.IngredientParser
import com.baastiklabs.pickless.core.label.ParsedLabel
import com.baastiklabs.pickless.core.text.TextNorm
import kotlin.math.pow

/** A correction to a food's FODMAP numbers: the user's own, or one found by a web lookup. */
data class FodmapCorrection(val limits: List<FodmapLimit>, val source: FodmapSource, val note: String? = null)

/**
 * Turns a typed food, a dish or a scanned product into a verdict against the avoid list plus a
 * FODMAP estimate. Pure logic: the app passes in the avoid list, personal tolerances and corrections.
 */
class Analyzer(val kb: KnowledgeBase) {

    data class Context(
        val avoid: List<AvoidItem>,
        /** Per-group multiplier from reintroduction results: 0 = tolerated, 0.6 = OK in small amounts, 1.5 = sensitive. */
        val tolerance: Map<FodmapGroup, Double> = emptyMap(),
        /** Keyed "food:<id>" or "product:<code>". */
        val corrections: Map<String, FodmapCorrection> = emptyMap(),
    )

    private class TagSource(val tags: Set<String>, val text: String, val entry: FoodEntry?, val minor: Boolean = false)

    // ---------------------------------------------------------------- typed foods and dishes

    fun analyzeEntry(entry: FoodEntry, ctx: Context): Analysis {
        val correction = ctx.corrections["food:${entry.id}"]
        val profile = correction?.let { FodmapProfile.ofLimits(entry.id, entry.name, it.limits) } ?: kb.profile(entry)
        val comps = kb.componentIds(entry).mapNotNull { kb[it] }
        val sources = mutableListOf(TagSource(entry.tags - "whole", entry.name, entry))
        comps.forEach { sources += TagSource(it.tags - "whole", it.name, it) }
        val findings = evaluate(ctx.avoid, sources, emptyList(), emptyList(), emptyList(), emptyList(), TextNorm.normalize(entry.name + " " + entry.aliases.joinToString(" ")))
        val notes = mutableListOf<String>()
        if (entry.kind == EntryKind.DISH) notes += "Based on a typical recipe (${comps.joinToString { it.name.lowercase() }}). Restaurants vary."
        if (entry.id == "garlic-oil") notes += "FODMAPs don't dissolve in oil, so garlic-infused oil is low FODMAP."
        if ("maybe-hidden" in entry.tags) notes += "These vary a lot by brand: scanning the barcode gives a real answer."
        val features = Features(
            foods = setOf(entry.id),
            ingredients = (listOf(entry) + comps).flatMap { kb.lineage(it.id) }.map { it.id }.toSet(),
            tags = kb.allTags(entry),
            fodmapPerGram = profile.rates.mapKeys { it.key.id }.mapValues { it.value.unitsPerGram },
        )
        val ingredients = comps.map { c -> describe(c, c.name, 1, false, ctx) }
        val source = correction?.source ?: FodmapSource.KNOWN
        return finish(entry.name, categoryLabel(entry), findings, profile, entry.defaultPortion, entry.portions, source,
            Confidence.HIGH, notes, features, ingredients, emptyList(), entry.id, ctx)
    }

    /**
     * For a typed food the knowledge base doesn't know: use any foods named in it, or the kind of
     * food it sounds like. Always low confidence.
     */
    fun guess(query: String, ctx: Context): Analysis? {
        val stems = TextNorm.stems(query)
        val matches = kb.matchAliases(stems).map { it.entry }.distinctBy { it.id }
        if (matches.isNotEmpty()) {
            val w = 1.0 / matches.size
            val profile = FodmapProfile.combine(matches.map { kb.profile(it) to w })
            val sources = matches.flatMap { m -> listOf(TagSource(kb.allTags(m), m.name, m)) + kb.componentIds(m).mapNotNull { kb[it] }.map { TagSource(it.tags - "whole", it.name, it) } }
            val findings = evaluate(ctx.avoid, sources, emptyList(), emptyList(), emptyList(), emptyList(), TextNorm.normalize(query))
            val features = Features(
                foods = emptySet(),
                ingredients = matches.flatMap { m -> (listOf(m.id) + kb.componentIds(m)).flatMap { kb.lineage(it).map { e -> e.id } } }.toSet(),
                tags = matches.flatMap { kb.allTags(it) }.toSet(),
                fodmapPerGram = profile.rates.mapKeys { it.key.id }.mapValues { it.value.unitsPerGram },
            )
            val notes = listOf("Pickless doesn't know \"${query.trim()}\" as a whole, so this guess is built from: ${matches.joinToString { it.name.lowercase() }}.")
            return finish(query.trim(), "Educated guess", findings, profile, Portion("serving", 150.0), listOf(Portion("serving", 150.0)),
                FodmapSource.GUESS, Confidence.LOW, notes, features, matches.map { describe(it, it.name, 0, false, ctx) }, emptyList(), null, ctx)
        }
        val words = TextNorm.words(query).toSet()
        val (limits, why) = when {
            words.any { it.startsWith("bean") || it.startsWith("lentil") || it == "dal" || it == "legume" } -> listOf(FodmapLimit(FodmapGroup.GOS, 40.0, 80.0)) to "beans and lentils are usually high in GOS"
            words.any { it.endsWith("nut") || it.endsWith("nuts") } -> listOf(FodmapLimit(FodmapGroup.GOS, 10.0, 20.0)) to "many nuts are moderate in GOS"
            words.any { it.contains("juice") } -> listOf(FodmapLimit(FodmapGroup.FRUCTOSE, 60.0, 120.0)) to "fruit juices concentrate fructose"
            words.any { it.contains("fruit") || it.contains("berry") || it.contains("berries") } -> listOf(FodmapLimit(FodmapGroup.FRUCTOSE, 80.0, 160.0)) to "fruit varies; this assumes a typical fruit"
            else -> return null
        }
        val profile = FodmapProfile.ofLimits("guess", query, limits)
        return finish(query.trim(), "Educated guess", emptyList(), profile, Portion("serving", 100.0), listOf(Portion("serving", 100.0)),
            FodmapSource.GUESS, Confidence.LOW, listOf("Pickless doesn't know this food. Guessing because $why."),
            Features(fodmapPerGram = profile.rates.mapKeys { it.key.id }.mapValues { it.value.unitsPerGram }), emptyList(), emptyList(), null, ctx)
    }

    // ---------------------------------------------------------------- products

    private class NodeMatch(
        val node: IngredientNode,
        val depth: Int,
        var entries: List<FoodEntry>,
        val parent: NodeMatch?,
    ) {
        val children = mutableListOf<NodeMatch>()
        var share = 0.0
        var extraTags = mutableSetOf<String>()
        var droppedTags = mutableSetOf<String>()
        fun tagsOf(e: FoodEntry): Set<String> = (e.tags - "whole" - droppedTags) + extraTags
        fun anyMatched(): Boolean = entries.isNotEmpty() || children.any { it.anyMatched() }
    }

    fun analyzeProduct(p: ProductInput, ctx: Context): Analysis {
        val label = IngredientParser.parse(p.ingredientsText)
        val roots = label.ingredients.map { matchNode(it, 0, null) }
        assignShares(roots, 1.0)
        applyCaps(roots, p)

        val identity = identityOf(p, roots)
        val nameWords = TextNorm.words(listOfNotNull(p.name, p.brand).joinToString(" "))

        // Tag sources: every matched ingredient, plus what the name and category say.
        val sources = mutableListOf<TagSource>()
        fun walk(nm: NodeMatch) {
            for (e in nm.entries) sources += TagSource(nm.tagsOf(e), nm.node.text, e, nm.node.minor)
            if (nm.entries.isEmpty() && nm.extraTags.isNotEmpty()) sources += TagSource(nm.extraTags, nm.node.text, null, nm.node.minor)
            nm.children.forEach(::walk)
        }
        roots.forEach(::walk)
        val bakedName = nameWords.any { it == "baked" || it == "popped" || it == "oven" } || nameWords.joinToString(" ").contains("air fried")
        identity?.let {
            val tags = if (bakedName && "fried" in it.tags) it.tags - "whole" - "fried" + "fried-light" else it.tags - "whole"
            sources += TagSource(tags, p.name ?: it.name, it)
        }
        val nameMatches = if (label.isEmpty) kb.matchAliases(TextNorm.stems(p.name ?: "")).map { it.entry } else emptyList()
        if (identity == null) nameMatches.forEach { sources += TagSource(it.tags - "whole", "${it.name} (from the name)", it) }
        sources += productLevelTags(p, nameWords, identity)

        val fullText = TextNorm.normalize(listOfNotNull(p.name, p.ingredientsText).joinToString(" "))
        val findings = evaluate(ctx.avoid, sources, label.contains, label.mayContain, p.allergens, p.traces, fullText)

        // FODMAP
        val correction = p.code?.let { ctx.corrections["product:$it"] }
        val certified = p.labels.any { it.contains("fodmap", ignoreCase = true) }
        var source: FodmapSource
        var profile: FodmapProfile
        when {
            correction != null -> { profile = FodmapProfile.ofLimits("product:${p.code}", p.name ?: "this product", correction.limits); source = correction.source }
            identity != null && (identity.limits.isNotEmpty() || identity.components.isNotEmpty()) -> {
                profile = kb.profile(identity) + fodmapFromNodes(roots, identity)
                source = FodmapSource.KNOWN
            }
            !label.isEmpty -> { profile = fodmapFromNodes(roots, identity); source = FodmapSource.ESTIMATED }
            identity != null -> { profile = FodmapProfile.NONE; source = FodmapSource.KNOWN }
            nameMatches.isNotEmpty() -> {
                profile = FodmapProfile.combine(nameMatches.map { kb.profile(it) to 1.0 / nameMatches.size })
                source = FodmapSource.GUESS
            }
            else -> { profile = FodmapProfile.NONE; source = FodmapSource.NONE }
        }
        val portion = when {
            p.servingGrams != null && p.servingGrams > 0 -> Portion(p.servingLabel?.takeIf { it.isNotBlank() } ?: "${fmt(p.servingGrams)} g", p.servingGrams).let { Portion("serving", it.grams) }
            identity != null -> identity.defaultPortion
            else -> Portion("serving", 100.0)
        }
        if (certified && correction == null) {
            val worst = profile.loadsAt(portion.grams).maxOfOrNull { it.units } ?: 0.0
            if (worst > 0.95) profile = profile.scaled(0.95 / worst)
            source = FodmapSource.CERTIFIED
        }

        // Confidence and notes
        val total = roots.sumOf { it.share }.coerceAtLeast(1e-9)
        val matchedShare = roots.filter { it.anyMatched() }.sumOf { it.share } / total
        val unknown = mutableListOf<String>()
        fun unknownWalk(nm: NodeMatch) {
            if (!nm.anyMatched() && nm.node.text.isNotBlank()) unknown += nm.node.text else nm.children.forEach(::unknownWalk)
        }
        roots.forEach(::unknownWalk)
        val topUnknown = roots.take(3).any { !it.anyMatched() }
        val confidence = when {
            label.isEmpty && identity == null -> Confidence.LOW
            label.isEmpty -> Confidence.MEDIUM
            identity != null && matchedShare >= 0.6 -> Confidence.HIGH
            matchedShare >= 0.85 && !topUnknown -> Confidence.HIGH
            matchedShare >= 0.6 -> Confidence.MEDIUM
            else -> Confidence.LOW
        }
        val notes = mutableListOf<String>()
        if (label.isEmpty) notes += "No ingredient list on file for this product. Snap the label to check it properly."
        else if (source == FodmapSource.ESTIMATED) notes += "Labels don't give amounts, so FODMAP is estimated from the order of the ingredients."
        if (certified) notes += "The label carries a low-FODMAP certification logo."
        if (unknown.isNotEmpty()) notes += "Didn't recognize: ${unknown.take(6).joinToString()}${if (unknown.size > 6) "…" else ""}."
        if (findings.any { f -> f.level != HitLevel.AVOID && f.evidence.any { it.reason.contains("refined") } }) {
            notes += "Refined soy (lecithin, soybean oil) has almost no soy protein. Set Soy to Strict if you want it counted as soy."
        }

        val features = Features(
            foods = setOfNotNull(identity?.id),
            ingredients = buildSet {
                fun addAll(nm: NodeMatch) { nm.entries.forEach { e -> kb.lineage(e.id).forEach { add(it.id) } }; nm.children.forEach(::addAll) }
                roots.forEach(::addAll)
                identity?.let { kb.lineage(it.id).forEach { e -> add(e.id) } }
                nameMatches.forEach { kb.lineage(it.id).forEach { e -> add(e.id) } }
            },
            tags = sources.flatMap { it.tags }.toSet(),
            fodmapPerGram = profile.rates.mapKeys { it.key.id }.mapValues { it.value.unitsPerGram },
        )
        val described = mutableListOf<MatchedIngredient>()
        fun describeWalk(nm: NodeMatch) {
            val e = nm.entries.firstOrNull()
            described += if (e != null) describe(e, nm.node.text, nm.depth, nm.node.minor, ctx, nm.tagsOf(e))
            else MatchedIngredient(nm.node.text, null, null, emptyList(), emptyList(), emptyList(), nm.depth, nm.node.minor)
            nm.children.forEach(::describeWalk)
        }
        roots.forEach(::describeWalk)
        val portions = buildList {
            add(portion)
            if (portion.grams != 100.0) add(Portion("100 g", 100.0))
            identity?.portions?.filter { it.grams != portion.grams }?.let(::addAll)
        }.distinctBy { it.name to it.grams }
        val title = p.name?.takeIf { it.isNotBlank() } ?: identity?.name ?: "Unnamed product"
        return finish(title, p.brand, findings, profile, portion, portions, source, confidence, notes, features, described, unknown, identity?.id, ctx)
    }

    /** Parse a label (for the camera flow) without a database record. */
    fun analyzeLabelText(text: String, name: String?, ctx: Context): Analysis =
        analyzeProduct(ProductInput(name = name, ingredientsText = text, source = "label"), ctx)

    fun parse(text: String?): ParsedLabel = IngredientParser.parse(text)

    private fun matchNode(node: IngredientNode, depth: Int, parent: NodeMatch?): NodeMatch {
        val stems = node.stems
        var matches = kb.matchAliases(stems)
        // "coconut flour": a food followed by bare "flour" is that food's flour, not wheat.
        matches = matches.filterNot { m -> m.entry.id == "wheat-flour" && m.end - m.start == 1 && stems[m.start] == "flour" && matches.any { it.end == m.start } }
        var entries = matches.map { it.entry }.distinctBy { it.id }
        val refinedContext = stems.any { it == "oil" || it == "lecithin" || it == "shortening" } ||
            (parent?.entries?.any { "maybe-soy" in it.tags || it.id == "soybean-oil" || it.id == "soy-lecithin" } == true)
        val lecithinContext = stems.contains("lecithin") || parent?.entries?.any { it.id == "lecithin" || it.id == "soy-lecithin" } == true
        if (refinedContext) {
            entries = entries.map { e ->
                if ("soy" in e.tags && "soy-refined" !in e.tags && (e.id == "soybeans" || e.id == "soy-protein")) {
                    kb.require(if (lecithinContext) "soy-lecithin" else "soybean-oil")
                } else e
            }.distinctBy { it.id }
        }
        val nm = NodeMatch(node, depth, entries, parent)
        // Fried words on the label itself.
        val w = node.words
        for ((i, word) in w.withIndex()) {
            val prev = w.getOrNull(i - 1)
            if (word == "fried" || word == "prefried" || word == "fry") {
                when (prev) {
                    "air", "freeze" -> {}
                    "pan", "stir", "lightly" -> nm.extraTags += "fried-light"
                    else -> nm.extraTags += "fried"
                }
            }
        }
        node.children.forEach { nm.children += matchNode(it, depth + 1, nm) }
        // A "vegetable oil (canola, sunflower)" or "lecithin (sunflower)" that names its sources
        // is no longer a maybe-soy: either a child says soy (and is flagged itself) or it isn't soy.
        if (nm.entries.any { "maybe-soy" in it.tags } && nm.children.isNotEmpty()) {
            nm.droppedTags += "maybe-soy"
        }
        return nm
    }

    /**
     * Estimated share of the product for each ingredient. Labels list ingredients by weight, so
     * shares decrease geometrically; stated percentages are kept; anything after "2% or less" gets 0.6%.
     */
    private fun assignShares(nodes: List<NodeMatch>, total: Double) {
        if (nodes.isEmpty()) return
        val explicit = nodes.map { it.node.percent?.takeIf { p -> p in 0.0..100.0 }?.let { p -> p / 100.0 * total } }
        val minorEach = if (nodes.count { it.node.minor } > 10) 0.004 * total else 0.006 * total
        val shares = DoubleArray(nodes.size)
        var used = 0.0
        for (i in nodes.indices) {
            when {
                explicit[i] != null -> { shares[i] = explicit[i]!!; used += shares[i] }
                nodes[i].node.minor -> { shares[i] = minorEach; used += minorEach }
            }
        }
        val majors = nodes.indices.filter { explicit[it] == null && !nodes[it].node.minor }
        val remaining = (total - used).coerceAtLeast(0.0)
        if (majors.isNotEmpty()) {
            val weights = majors.mapIndexed { k, _ -> 0.6.pow(k) }
            val sum = weights.sum()
            majors.forEachIndexed { k, idx -> shares[idx] = remaining * weights[k] / sum }
            // Keep the order consistent with any stated percentages around each ingredient.
            repeat(3) {
                for (idx in majors) {
                    val before = (0 until idx).mapNotNull { explicit[it] }.minOrNull()
                    val after = (idx + 1 until nodes.size).mapNotNull { explicit[it] }.maxOrNull()
                    if (before != null && shares[idx] > before) shares[idx] = before
                    if (after != null && shares[idx] < after) shares[idx] = after
                }
                val s = majors.sumOf { shares[it] }
                if (s > 0) majors.forEach { shares[it] *= remaining / s }
            }
        }
        for (i in nodes.indices) {
            nodes[i].share = shares[i]
            assignShares(nodes[i].children, shares[i])
        }
    }

    /** Use the nutrition panel to keep sweetener, sugar-alcohol and added-fiber estimates believable. */
    private fun applyCaps(roots: List<NodeMatch>, p: ProductInput) {
        val all = mutableListOf<NodeMatch>()
        fun walk(n: NodeMatch) { all += n; n.children.forEach(::walk) }
        roots.forEach(::walk)
        fun cap(filter: (FoodEntry) -> Boolean, limitPer100: Double?) {
            if (limitPer100 == null) return
            val hits = all.filter { n -> n.entries.any(filter) }
            val sum = hits.sumOf { it.share }
            val max = limitPer100 / 100.0
            if (sum > max && sum > 0) hits.forEach { it.share *= max / sum }
        }
        cap({ it.category == "sweetener" || it.id == "hfcs" || it.id == "fructose" }, p.sugarsPer100)
        cap({ "polyol" in it.tags }, p.polyolsPer100)
        cap({ "fiber-additive" in it.tags }, p.fiberPer100)
    }

    private fun related(a: FoodEntry, b: FoodEntry): Boolean {
        val la = kb.lineage(a.id).map { it.id }.toSet()
        return kb.lineage(b.id).any { it.id in la }
    }

    private fun fodmapFromNodes(roots: List<NodeMatch>, identity: FoodEntry?): FodmapProfile {
        val parts = mutableListOf<Pair<FodmapProfile, Double>>()
        fun walk(nm: NodeMatch) {
            val withData = nm.entries.filter { e -> (e.limits.isNotEmpty() || e.components.isNotEmpty()) && (identity == null || !related(e, identity)) }
            if (withData.isNotEmpty()) {
                val each = nm.share / withData.size
                withData.forEach { parts += kb.profile(it) to each }
                return
            }
            if (identity != null && nm.entries.any { related(it, identity) }) return // describes the product itself
            nm.children.forEach(::walk)
        }
        roots.forEach(::walk)
        return FodmapProfile.combine(parts)
    }

    private val offCategoryIdentity = mapOf(
        "en:almond-milks" to "almond-milk", "en:almond-based-drinks" to "almond-milk",
        "en:soy-milks" to "soy-milk-whole", "en:soy-based-drinks" to "soy-milk-whole", "en:soya-milks" to "soy-milk-whole",
        "en:oat-based-drinks" to "oat-milk", "en:oat-milks" to "oat-milk",
        "en:rice-based-drinks" to "rice-milk", "en:rice-milks" to "rice-milk",
        "en:cashew-based-drinks" to "cashew-milk", "en:coconut-milks" to "coconut-milk",
        "en:lactose-free-milks" to "lactose-free-milk", "en:skimmed-milks" to "milk", "en:semi-skimmed-milks" to "milk",
        "en:whole-milks" to "milk", "en:cow-milks" to "milk",
    )

    /** What the product as a whole is, when its numbers are better described by a known food (milks, breads...). */
    private fun identityOf(p: ProductInput, roots: List<NodeMatch>): FoodEntry? {
        p.categories.firstNotNullOfOrNull { offCategoryIdentity[it] }?.let { id -> return refineIdentity(kb.require(id), roots) }
        val name = p.name ?: return null
        val stems = TextNorm.stems(name)
        val hits = kb.matchAliases(stems).filter { it.entry.isWhole }
        val best = hits.maxWithOrNull(compareBy<com.baastiklabs.pickless.core.kb.AliasMatch> { it.end - it.start }.thenBy { it.end }) ?: return null
        return refineIdentity(best.entry, roots)
    }

    private fun refineIdentity(e: FoodEntry, roots: List<NodeMatch>): FoodEntry {
        if (e.id == "soy-milk-whole") {
            val ids = mutableSetOf<String>()
            fun walk(n: NodeMatch) { n.entries.forEach { ids += it.id }; n.children.forEach(::walk) }
            roots.forEach(::walk)
            if ("soy-protein" in ids && "soybeans" !in ids) return kb.require("soy-milk-protein")
        }
        return e
    }

    private val hotExceptions = listOf("hot dog", "hot dogs", "hot chocolate", "hot cocoa", "hot cereal", "hot pocket", "hot pockets", "hot fudge", "hot cross", "hot tea", "hot water", "hot cakes", "hotcakes")
    private val spicyWords = setOf("spicy", "fiery", "flamin", "flaming", "jalapeno", "habanero", "chipotle", "sriracha", "buffalo", "nashville", "inferno", "diablo", "picante", "scorpion", "reaper", "ghost", "harissa", "gochujang", "kimchi", "szechuan", "sichuan", "vindaloo", "hellfire", "blazin", "firecracker")
    private val friedWords = setOf("fried", "chips", "chip", "crisps", "fries", "donut", "donuts", "doughnut", "doughnuts", "tempura", "battered", "churro", "churros", "fritter", "fritters", "tots", "rinds", "chicharrones", "funnel")

    private fun productLevelTags(p: ProductInput, words: List<String>, identity: FoodEntry?): List<TagSource> {
        val out = mutableListOf<TagSource>()
        val text = words.joinToString(" ")
        val baked = words.any { it == "baked" || it == "popped" || it == "oven" } || text.contains("air fried")
        val cats = p.categories.map { it.lowercase() }
        val friedCategory = cats.firstOrNull { c -> listOf("chips", "crisps", "fries", "doughnut", "fried", "pork-rinds", "onion-rings").any { c.contains(it) } }
        val friedWord = words.firstOrNull { it in friedWords }
        when {
            baked -> if (friedCategory != null || friedWord != null) out += TagSource(setOf("fried-light"), "\"${if (text.contains("air fried")) "air fried" else "baked"}\" in the name", null)
            friedWord != null && identity?.tags?.contains("fried") != true -> out += TagSource(setOf("fried"), "\"$friedWord\" in the name", null)
            friedCategory != null -> out += TagSource(setOf("fried"), "category: ${friedCategory.substringAfter(':').replace('-', ' ')}", null)
        }
        val fat = p.fatPer100
        val snacky = words.any { it in setOf("snack", "snacks", "puffs", "curls", "crunchy", "crispy") } || cats.any { it.contains("snack") || it.contains("appetizer") }
        if (!baked && friedWord == null && friedCategory == null && fat != null && fat >= 22.0 && snacky) {
            out += TagSource(setOf("maybe-fried"), "${fmt(fat)} g fat per 100 g, typical of fried snacks", null)
        }
        val hot = words.indices.any { i -> words[i] == "hot" && hotExceptions.none { ex -> text.contains(ex) } }
        val spicyWord = words.firstOrNull { it in spicyWords }
        val mild = words.any { it == "mild" || it == "medium" }
        when {
            spicyWord != null || hot -> out += TagSource(setOf(if (mild && spicyWord == null) "spicy-mild" else "spicy"), "\"${spicyWord ?: "hot"}\" in the name", null)
            words.contains("cajun") || words.contains("curry") || words.contains("chili") -> out += TagSource(setOf("spicy-mild"), "\"${words.first { it == "cajun" || it == "curry" || it == "chili" }}\" in the name", null)
        }
        return out
    }

    // ---------------------------------------------------------------- avoid list

    private fun evaluate(
        avoid: List<AvoidItem>,
        sources: List<TagSource>,
        containsWords: List<String>,
        traceWords: List<String>,
        offAllergens: List<String>,
        offTraces: List<String>,
        fullText: String,
    ): List<AvoidFinding> {
        val out = mutableListOf<AvoidFinding>()
        for (item in avoid) {
            if (!item.status.flags) continue
            val hard = mutableListOf<Evidence>()
            val soft = mutableListOf<Evidence>()
            val trace = mutableListOf<Evidence>()
            val cat = item.category
            if (cat != null) {
                for (s in sources) {
                    val hit = s.tags.intersect(cat.avoidTags)
                    val maybe = s.tags.intersect(cat.cautionTags)
                    when {
                        hit.isNotEmpty() -> hard += Evidence(s.text, reason(s, cat.id, hit.first()))
                        maybe.isNotEmpty() -> when (item.strictness) {
                            Strictness.STRICT -> hard += Evidence(s.text, reason(s, cat.id, maybe.first()))
                            Strictness.STANDARD -> soft += Evidence(s.text, reason(s, cat.id, maybe.first()))
                            Strictness.RELAXED -> {}
                        }
                    }
                }
                val statementHits = containsWords.filter { w -> cat.allergenWords.any { a -> w == a || TextNorm.words(w).contains(a) } }
                statementHits.forEach { hard += Evidence("Contains: $it", "listed in the label's allergen statement") }
                if (statementHits.isEmpty()) offAllergens.filter { it in cat.offAllergens }.forEach {
                    hard += Evidence(it.substringAfter(':').replace('-', ' '), "listed as an allergen in the product database")
                }
                traceWords.filter { w -> cat.allergenWords.any { a -> w == a || TextNorm.words(w).contains(a) } }.forEach {
                    trace += Evidence("May contain $it", "shared-equipment or \"may contain\" warning")
                }
                offTraces.filter { it in cat.offAllergens }.forEach { trace += Evidence("May contain ${it.substringAfter(':')}", "traces warning in the product database") }
            }
            item.foodId?.let { fid ->
                for (s in sources) {
                    val e = s.entry ?: continue
                    if (kb.lineage(e.id).any { it.id == fid }) hard += Evidence(s.text, "${e.name} is ${kb[fid]?.name?.lowercase() ?: fid}")
                }
            }
            item.term?.let { term ->
                val t = TextNorm.normalize(term)
                if (t.isNotEmpty() && Regex("\\b${Regex.escape(t)}s?\\b").containsMatchIn(fullText)) hard += Evidence(term, "mentioned on the label")
            }
            var level = when {
                hard.isNotEmpty() -> HitLevel.AVOID
                soft.isNotEmpty() -> HitLevel.CAUTION
                trace.isNotEmpty() -> when (item.traces) {
                    TracePolicy.AVOID -> HitLevel.AVOID
                    TracePolicy.CAUTION -> HitLevel.TRACE
                    TracePolicy.IGNORE -> null
                }
                else -> null
            } ?: continue
            if (item.status == AvoidStatus.SMALL_OK && level == HitLevel.AVOID) level = HitLevel.CAUTION
            val evidence = (hard + soft + trace).distinctBy { it.text.lowercase() to it.reason }
            out += AvoidFinding(item, level, evidence)
        }
        return out
    }

    private fun reason(s: TagSource, categoryId: String, tag: String): String {
        val e = s.entry
        val what = e?.name?.lowercase() ?: return when (tag) {
            "fried" -> "this looks like fried food"
            "fried-light" -> "may be pan-fried or air-fried"
            "maybe-fried" -> "probably fried"
            "spicy" -> "sold as spicy"
            "spicy-mild" -> "may be a little spicy"
            else -> tag
        }
        val cat = Categories.byId(categoryId)?.label?.lowercase() ?: categoryId
        return when (tag) {
            "soy-refined" -> "$what is refined soy (very little soy protein)"
            "maybe-soy" -> if (e.id == "lecithin") "lecithin is usually from soy when the source isn't named" else "\"$what\" is often soybean oil in the US"
            "maybe-dairy" -> "$what may contain milk"
            "fried-light" -> "$what is usually pan-fried"
            "maybe-fried" -> "$what is often fried"
            "spicy-mild" -> "$what can be a little spicy"
            "fried" -> "$what is fried"
            "spicy" -> "$what is spicy"
            else -> "$what is $cat"
        }
    }

    // ---------------------------------------------------------------- shared

    private fun describe(e: FoodEntry, text: String, depth: Int, minor: Boolean, ctx: Context, tags: Set<String> = e.tags - "whole"): MatchedIngredient {
        val avoidLabels = mutableListOf<String>()
        val cautionLabels = mutableListOf<String>()
        for (item in ctx.avoid) {
            if (!item.status.flags) continue
            val c = item.category
            if (c != null) {
                if (tags.any { it in c.avoidTags }) avoidLabels += item.label
                else if (tags.any { it in c.cautionTags } && item.strictness != Strictness.RELAXED) {
                    if (item.strictness == Strictness.STRICT) avoidLabels += item.label else cautionLabels += item.label
                }
            }
            if (item.foodId != null && kb.lineage(e.id).any { it.id == item.foodId }) avoidLabels += item.label
        }
        val groups = (if (e.limits.isNotEmpty()) e.limits.map { it.group } else kb.profile(e).rates.keys.toList()).distinct()
        return MatchedIngredient(text, e.id, e.name, avoidLabels.distinct(), cautionLabels.distinct(), groups, depth, minor)
    }

    private fun categoryLabel(e: FoodEntry): String = when (e.kind) {
        EntryKind.DISH -> "Dish"
        else -> e.category.replace('-', ' ').replaceFirstChar { it.uppercase() }
    }

    private fun finish(
        title: String, subtitle: String?, findings: List<AvoidFinding>, profile: FodmapProfile, portion: Portion, portions: List<Portion>,
        source: FodmapSource, confidence: Confidence, notes: List<String>, features: Features, ingredients: List<MatchedIngredient>,
        unknown: List<String>, identityId: String?, ctx: Context,
    ): Analysis {
        val reasons = mutableListOf<String>()
        val avoidHits = findings.filter { it.level == HitLevel.AVOID }
        val cautions = findings.filter { it.level != HitLevel.AVOID }
        avoidHits.forEach { reasons += "Contains ${it.item.label.lowercase()}" }
        cautions.forEach { reasons += if (it.level == HitLevel.TRACE) "May contain ${it.item.label.lowercase()}" else "Possibly ${it.item.label.lowercase()}" }
        val score = profile.scoreAt(portion.grams, ctx.tolerance)
        val level = FodmapScore.level(score)
        if (level != FodmapLevel.LOW) reasons += "${level.label} FODMAP at ${portionText(portion)}"
        val verdict = when {
            avoidHits.isNotEmpty() -> Verdict.AVOID
            cautions.isNotEmpty() || level != FodmapLevel.LOW -> Verdict.CAUTION
            source == FodmapSource.NONE && ingredients.isEmpty() -> Verdict.UNKNOWN
            else -> Verdict.GOOD
        }
        return Analysis(title, subtitle, verdict, reasons, findings, profile, portion, portions, source, confidence, notes, features, ingredients, unknown, identityId, ctx.tolerance)
    }

    /** Fold several foods eaten together into one FODMAP load ("stacking"). */
    fun meal(items: List<Pair<FodmapProfile, Double>>, tolerance: Map<FodmapGroup, Double> = emptyMap()): MealFodmap {
        val total = FodmapProfile.combine(items)
        val loads = total.loadsAt(1.0, tolerance)
        val score = FodmapScore.overall(loads)
        val worstSingle = items.maxOfOrNull { (p, g) -> p.scoreAt(g, tolerance) } ?: 0.0
        return MealFodmap(loads, score, worstSingle, FodmapScore.level(score) > FodmapScore.level(worstSingle))
    }

    companion object {
        fun fmt(v: Double): String = if (v == Math.floor(v)) v.toInt().toString() else String.format("%.1f", v)
        fun portionText(p: Portion): String = when (p.name) {
            "serving" -> "1 serving (${fmt(p.grams)} g)"
            "each" -> "1 (${fmt(p.grams)} g)"
            "100 g" -> "100 g"
            else -> "1 ${p.name}"
        }
    }
}

data class MealFodmap(
    val loads: List<com.baastiklabs.pickless.core.fodmap.GroupLoad>,
    val score: Double,
    val worstSingleScore: Double,
    /** The meal is worse than any one food in it: several moderate foods added up. */
    val stacked: Boolean,
)
