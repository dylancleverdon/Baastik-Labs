package com.baastiklabs.pickless.core.kb

import com.baastiklabs.pickless.core.fodmap.FodmapGroup
import com.baastiklabs.pickless.core.fodmap.FodmapLimit
import com.baastiklabs.pickless.core.fodmap.FodmapProfile
import com.baastiklabs.pickless.core.text.TextNorm

enum class EntryKind { FOOD, INGREDIENT, DISH }

data class Portion(val name: String, val grams: Double) {
    val label: String get() = when (name) {
        "each" -> "1"
        "serving" -> "1 serving"
        else -> "1 $name"
    }
}

data class Component(val id: String, val grams: Double)

/** One row of foods.txt. [tags] includes everything inherited from parents ("whole" is never inherited). */
class FoodEntry(
    val id: String,
    val kind: EntryKind,
    val category: String,
    val name: String,
    val aliases: List<String>,
    val parentId: String?,
    val ownTags: Set<String>,
    val limits: List<FodmapLimit>,
    val portions: List<Portion>,
    val components: List<Component>,
) {
    var tags: Set<String> = ownTags
        internal set

    val isWhole: Boolean get() = "whole" in ownTags
    val defaultPortion: Portion get() = portions.firstOrNull() ?: Portion("serving", 100.0)
    val searchable: Boolean get() = kind != EntryKind.INGREDIENT

    override fun toString(): String = "FoodEntry($id)"
}

data class AliasMatch(val entry: FoodEntry, val start: Int, val end: Int, val alias: String)

data class SearchHit(val entry: FoodEntry, val score: Double, val matched: String)

class KnowledgeBase(entryList: List<FoodEntry>) {
    val entries: Map<String, FoodEntry> = entryList.associateBy { it.id }
    private val trie = Trie()
    /** Stemmed alias key -> ids that claimed it (more than one is a data error the tests report). */
    val aliasOwners: Map<String, List<String>>

    init {
        for (e in entryList) e.tags = resolveTags(e, mutableSetOf())
        val owners = linkedMapOf<String, MutableList<String>>()
        // Aliases first; a display name only claims words no alias uses.
        for (e in entryList) {
            val seen = mutableSetOf<String>()
            for (alias in e.aliases) {
                val stems = TextNorm.stems(alias)
                if (stems.isEmpty()) continue
                val key = stems.joinToString(" ")
                if (!seen.add(key)) continue
                val list = owners.getOrPut(key) { mutableListOf() }
                list.add(e.id)
                if (list.size == 1) trie.insert(stems, e.id, alias)
            }
        }
        for (e in entryList) {
            val stems = TextNorm.stems(e.name)
            val key = stems.joinToString(" ")
            if (stems.isNotEmpty() && key !in owners) {
                owners[key] = mutableListOf(e.id)
                trie.insert(stems, e.id, e.name)
            }
        }
        aliasOwners = owners
    }

    operator fun get(id: String): FoodEntry? = entries[id]

    fun require(id: String): FoodEntry = entries[id] ?: error("unknown food id $id")

    /** The entry and every parent above it. */
    fun lineage(id: String): List<FoodEntry> {
        val out = mutableListOf<FoodEntry>()
        var cur = entries[id]
        while (cur != null && out.none { it.id == cur!!.id }) {
            out.add(cur)
            cur = cur.parentId?.let { entries[it] }
        }
        return out
    }

    fun isRelated(a: String, b: String): Boolean = lineage(a).any { it.id == b } || lineage(b).any { it.id == a }

    /** Greedy longest-match of known aliases over stemmed words. */
    fun matchAliases(stems: List<String>): List<AliasMatch> {
        val out = mutableListOf<AliasMatch>()
        var i = 0
        while (i < stems.size) {
            val hit = trie.longest(stems, i)
            if (hit != null) {
                out.add(AliasMatch(require(hit.first), i, hit.second, hit.third))
                i = hit.second
            } else i++
        }
        return out
    }

    /** FODMAP profile per gram of the entry, from its own limits or (for dishes) its components. */
    fun profile(entry: FoodEntry, depth: Int = 0): FodmapProfile {
        if (entry.limits.isNotEmpty()) return FodmapProfile.ofLimits(entry.id, entry.name, entry.limits)
        if (entry.components.isEmpty() || depth > 4) return FodmapProfile.NONE
        val total = entry.defaultPortion.grams
        val parts = entry.components.filter { it.grams > 0 }.mapNotNull { c ->
            entries[c.id]?.let { profile(it, depth + 1) to c.grams / total }
        }
        return FodmapProfile.combine(parts)
    }

    /** Tags of the entry plus, for dishes, every component's tags. */
    fun allTags(entry: FoodEntry, depth: Int = 0): Set<String> {
        if (entry.components.isEmpty() || depth > 4) return entry.tags - "whole"
        val out = (entry.tags - "whole").toMutableSet()
        for (c in entry.components) if (c.grams > 0) entries[c.id]?.let { out += allTags(it, depth + 1) }
        return out
    }

    /** Component entries (recursively), for features and highlighting. */
    fun componentIds(entry: FoodEntry, depth: Int = 0): Set<String> {
        if (entry.components.isEmpty() || depth > 4) return emptySet()
        val out = mutableSetOf<String>()
        for (c in entry.components) if (c.grams > 0) entries[c.id]?.let { out += it.id; out += componentIds(it, depth + 1) }
        return out
    }

    /**
     * Search foods and dishes by name or alias. Every query word must prefix-match a word of the
     * alias (so typing works as you go); exact and whole-word matches rank first.
     */
    fun search(query: String, limit: Int = 25): List<SearchHit> {
        val q = TextNorm.words(query)
        if (q.isEmpty()) return emptyList()
        val qs = q.map(TextNorm::stem)
        val best = mutableMapOf<String, SearchHit>()
        for (e in entries.values) {
            if (!e.searchable) continue
            for (alias in listOf(e.name) + e.aliases) {
                val words = TextNorm.words(alias)
                if (words.isEmpty()) continue
                val stems = words.map(TextNorm::stem)
                var score = 0.0
                var ok = true
                for ((idx, w) in q.withIndex()) {
                    val s = qs[idx]
                    val exact = stems.indexOf(s)
                    if (exact >= 0) { score += if (exact == idx) 3.0 else 2.5; continue }
                    val pre = words.indexOfFirst { it.startsWith(w) }
                    if (pre >= 0) { score += 1.5; continue }
                    val fuzzy = w.length >= 5 && words.any { it.length >= 5 && editDistance(it, w) <= 1 }
                    if (fuzzy) { score += 0.8; continue }
                    ok = false; break
                }
                if (!ok) continue
                if (stems == qs) score += 4.0
                score -= (words.size - q.size).coerceAtLeast(0) * 0.35
                if (alias == e.name) score += 0.3
                if (e.kind == EntryKind.DISH) score -= 0.1
                val prev = best[e.id]
                if (prev == null || prev.score < score) best[e.id] = SearchHit(e, score, alias)
            }
        }
        return best.values.sortedWith(compareByDescending<SearchHit> { it.score }.thenBy { it.entry.name.length }).take(limit)
    }

    private fun resolveTags(e: FoodEntry, visiting: MutableSet<String>): Set<String> {
        if (!visiting.add(e.id)) return e.ownTags
        val parent = e.parentId?.let { entries[it] } ?: return e.ownTags
        return e.ownTags + (resolveTags(parent, visiting) - "whole")
    }

    private class Trie {
        private class Node {
            val next = HashMap<String, Node>()
            var id: String? = null
            var alias: String? = null
        }

        private val root = Node()

        fun insert(stems: List<String>, id: String, alias: String) {
            var n = root
            for (s in stems) n = n.next.getOrPut(s) { Node() }
            if (n.id == null) { n.id = id; n.alias = alias }
        }

        /** (id, endExclusive, alias) of the longest alias starting at [from]. */
        fun longest(stems: List<String>, from: Int): Triple<String, Int, String>? {
            var n = root
            var found: Triple<String, Int, String>? = null
            var i = from
            while (i < stems.size) {
                n = n.next[stems[i]] ?: break
                i++
                val id = n.id
                if (id != null) found = Triple(id, i, n.alias!!)
            }
            return found
        }
    }

    companion object {
        private const val RESOURCE = "/pickless/foods.txt"

        fun load(): KnowledgeBase {
            val stream = KnowledgeBase::class.java.getResourceAsStream(RESOURCE) ?: error("missing $RESOURCE")
            return parse(stream.bufferedReader().readText())
        }

        fun parse(text: String): KnowledgeBase {
            val list = mutableListOf<FoodEntry>()
            for ((n, raw) in text.lines().withIndex()) {
                val line = raw.trim()
                if (line.isEmpty() || line.startsWith("#")) continue
                val f = line.split('|')
                require(f.size == 10) { "foods.txt line ${n + 1}: expected 10 fields, got ${f.size}" }
                fun opt(s: String) = s.trim().takeUnless { it == "-" || it.isEmpty() }
                val kind = when (f[1].trim()) {
                    "F" -> EntryKind.FOOD; "I" -> EntryKind.INGREDIENT; "D" -> EntryKind.DISH
                    else -> error("foods.txt line ${n + 1}: bad kind ${f[1]}")
                }
                val limits = opt(f[7])?.split(';')?.map { part ->
                    val (g, v) = part.split(':')
                    val (lo, mo) = v.split('/')
                    FodmapLimit(FodmapGroup.byId(g.trim()) ?: error("line ${n + 1}: bad group $g"), lo.toDouble(), mo.toDouble())
                } ?: emptyList()
                val portions = opt(f[8])?.split(';')?.map { p -> val (k, v) = p.split('='); Portion(k.trim(), v.toDouble()) } ?: emptyList()
                val comps = opt(f[9])?.split(';')?.map { p -> val (k, v) = p.split(':'); Component(k.trim(), v.toDouble()) } ?: emptyList()
                list.add(
                    FoodEntry(
                        id = f[0].trim(), kind = kind, category = f[2].trim(), name = f[3].trim(),
                        aliases = opt(f[4])?.split(',')?.map { it.trim() }?.filter { it.isNotEmpty() } ?: emptyList(),
                        parentId = opt(f[5]), ownTags = opt(f[6])?.split(',')?.map { it.trim() }?.toSet() ?: emptySet(),
                        limits = limits, portions = portions, components = comps,
                    ),
                )
            }
            return KnowledgeBase(list)
        }

        fun editDistance(a: String, b: String): Int {
            if (kotlin.math.abs(a.length - b.length) > 1) return 2
            val dp = IntArray(b.length + 1) { it }
            for (i in 1..a.length) {
                var prev = dp[0]
                dp[0] = i
                for (j in 1..b.length) {
                    val tmp = dp[j]
                    dp[j] = minOf(dp[j] + 1, dp[j - 1] + 1, prev + if (a[i - 1] == b[j - 1]) 0 else 1)
                    prev = tmp
                }
            }
            return dp[b.length]
        }
    }
}
