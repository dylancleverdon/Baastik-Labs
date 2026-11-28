package com.baastiklabs.pickless.core.label

import com.baastiklabs.pickless.core.text.TextNorm

/** One ingredient on a label, with any sub-ingredients from its parentheses. */
data class IngredientNode(
    val text: String,
    val percent: Double?,
    /** Listed after "contains 2% or less of", so it is a small amount. */
    val minor: Boolean,
    val children: List<IngredientNode>,
) {
    val words: List<String> by lazy { TextNorm.words(text) }
    val stems: List<String> by lazy { words.map(TextNorm::stem) }
}

data class ParsedLabel(
    val raw: String,
    val ingredients: List<IngredientNode>,
    /** Allergen words from a "Contains: ..." statement. */
    val contains: List<String>,
    /** Allergen words from "May contain" / "made in a facility that also processes" statements. */
    val mayContain: List<String>,
) {
    val isEmpty: Boolean get() = ingredients.isEmpty()

    fun flatten(): List<IngredientNode> {
        val out = mutableListOf<IngredientNode>()
        fun walk(n: IngredientNode) { out.add(n); n.children.forEach(::walk) }
        ingredients.forEach(::walk)
        return out
    }
}

/**
 * Reads a US-style ingredient statement: nested parentheses, "contains 2% or less of", percentages,
 * "Contains: milk, soy." and "May contain ..." statements. Works on OCR text too: it is forgiving
 * about case, stray line breaks and missing punctuation.
 */
object IngredientParser {
    private val minorMarker = Regex(
        "(?i)\\b(?:and\\s+)?(?:contains\\s+)?(?:less\\s+than\\s+)?\\d+(?:\\.\\d+)?\\s*(?:%|percent)\\s*(?:or\\s+less)?\\s*(?:of)?\\s*(?:each\\s+of)?\\s*(?:the\\s+following)?\\s*:?",
    )
    private val traceStarts = listOf(
        "may contain", "may also contain", "may contain traces", "contains traces", "traces of",
        "made in a facility", "manufactured in a facility", "produced in a facility", "processed in a facility",
        "packaged in a facility", "made on shared equipment", "made on equipment", "produced on shared equipment",
        "manufactured on shared equipment", "processed on shared equipment", "made in a plant", "produced in a plant",
        "manufactured in a plant", "processed in a plant", "made on the same", "produced on equipment", "processed on equipment",
        "shared equipment", "same equipment", "same facility", "facility that also", "allergen information",
    )
    private val percentInText = Regex("(\\d+(?:\\.\\d+)?)\\s*%")
    private val noise = Regex("[*†‡^§¹²³]")

    fun parse(raw: String?): ParsedLabel {
        if (raw.isNullOrBlank()) return ParsedLabel(raw ?: "", emptyList(), emptyList(), emptyList())
        var text = raw.replace('\n', ' ').replace('\r', ' ').replace(noise, "")
            .replace('[', '(').replace(']', ')').replace('{', '(').replace('}', ')')
            .replace(Regex("\\s+"), " ").trim()
        // Drop anything before "Ingredients:".
        Regex("(?i)\\bingredients?\\s*[:\\-]").find(text)?.let { text = text.substring(it.range.last + 1) }
        // Cut off a nutrition panel or directions that OCR may have picked up after the list.
        Regex("(?i)\\b(nutrition facts|serving size|directions|distributed by|manufactured for|best by|keep refrigerated)\\b")
            .find(text)?.let { if (it.range.first > 20) text = text.substring(0, it.range.first) }

        val contains = mutableListOf<String>()
        val mayContain = mutableListOf<String>()
        val kept = StringBuilder()
        for (sentence in sentences(text)) {
            val s = sentence.trim()
            if (s.isEmpty()) continue
            val lower = s.lowercase()
            when {
                traceStarts.any { lower.startsWith(it) || lower.contains(" $it") && lower.indexOf(it) < 12 } -> mayContain += allergenList(after(lower, traceStarts))
                isAllergenStatement(lower) -> contains += allergenList(lower.substringAfter("contains").substringAfter("allergens").trimStart(':', ' '))
                else -> {
                    // A trailing "Contains: milk." glued onto the last ingredient without a period.
                    val idx = Regex("(?i)\\bcontains\\s*:\\s*").find(s)
                    if (idx != null && !minorMarker.containsMatchIn(s.substring(idx.range.first))) {
                        kept.append(s.substring(0, idx.range.first)).append(", ")
                        contains += allergenList(s.substring(idx.range.last + 1).lowercase())
                    } else kept.append(s).append(", ")
                }
            }
        }
        val items = splitTop(kept.toString())
        var minor = false
        val nodes = mutableListOf<IngredientNode>()
        for (item in items) {
            var t = item.trim()
            val m = minorMarker.find(t)
            if (m != null && (m.range.first == 0 || t.substring(0, m.range.first).isBlank() || t.lowercase().startsWith("and contains") || t.lowercase().startsWith("contains"))) {
                minor = true
                t = t.substring(m.range.last + 1).trim()
            } else if (m != null && Regex("(?i)(or less|less than)").containsMatchIn(m.value)) {
                // "salt, contains 2% or less of: x" where the marker sits mid-item.
                val before = t.substring(0, m.range.first).trim()
                if (before.isNotEmpty()) nodes += node(before, false)
                minor = true
                t = t.substring(m.range.last + 1).trim()
            }
            if (t.isBlank()) continue
            nodes += node(t, minor)
        }
        return ParsedLabel(raw, nodes.filter { it.text.isNotBlank() }, contains.distinct(), mayContain.distinct())
    }

    private fun node(text: String, minor: Boolean): IngredientNode {
        val open = text.indexOf('(')
        val head: String
        val children: List<IngredientNode>
        if (open >= 0) {
            val close = matchingParen(text, open)
            head = (text.substring(0, open) + " " + if (close + 1 < text.length) text.substring(close + 1) else "").trim()
            val inner = text.substring(open + 1, if (close > open) close else text.length)
            val innerItems = splitTop(inner)
            children = if (innerItems.size == 1 && percentOnly(innerItems[0])) emptyList()
            else innerItems.filter { it.isNotBlank() && !percentOnly(it) }.map { node(it.trim(), minor) }
            val pct = innerItems.firstOrNull { percentOnly(it) }?.let { percentInText.find(it)?.groupValues?.get(1)?.toDouble() }
            return IngredientNode(cleanHead(head), pct ?: percentInText.find(head)?.groupValues?.get(1)?.toDouble(), minor, children)
        }
        return IngredientNode(cleanHead(text), percentInText.find(text)?.groupValues?.get(1)?.toDouble(), minor, emptyList())
    }

    private fun cleanHead(s: String): String = s.replace(percentInText, " ").replace(Regex("(?i)^\\s*(and|or|and/or)\\s+"), "")
        .replace(Regex("\\s+"), " ").trim().trimEnd('.', ':', ';', ',').trim()

    private fun percentOnly(s: String): Boolean = s.trim().matches(Regex("(?i)(less than\\s+)?\\d+(\\.\\d+)?\\s*%\\s*(or less)?"))

    private fun matchingParen(s: String, open: Int): Int {
        var depth = 0
        for (i in open until s.length) {
            when (s[i]) { '(' -> depth++; ')' -> { depth--; if (depth == 0) return i } }
        }
        return s.length - 1
    }

    /** Split on commas and semicolons outside parentheses. */
    fun splitTop(s: String): List<String> {
        val out = mutableListOf<String>()
        var depth = 0
        val cur = StringBuilder()
        for (c in s) {
            when {
                c == '(' -> { depth++; cur.append(c) }
                c == ')' -> { depth = (depth - 1).coerceAtLeast(0); cur.append(c) }
                (c == ',' || c == ';') && depth == 0 -> { out += cur.toString(); cur.clear() }
                else -> cur.append(c)
            }
        }
        if (cur.isNotBlank()) out += cur.toString()
        return out.map { it.trim() }.filter { it.isNotEmpty() }
    }

    /** Split into sentences on periods that end a phrase (not decimals, not inside parentheses). */
    private fun sentences(s: String): List<String> {
        val out = mutableListOf<String>()
        var depth = 0
        val cur = StringBuilder()
        for (i in s.indices) {
            val c = s[i]
            when {
                c == '(' -> { depth++; cur.append(c) }
                c == ')' -> { depth = (depth - 1).coerceAtLeast(0); cur.append(c) }
                c == '.' && depth == 0 && !(i > 0 && s[i - 1].isDigit() && i + 1 < s.length && s[i + 1].isDigit()) -> { out += cur.toString(); cur.clear() }
                else -> cur.append(c)
            }
        }
        if (cur.isNotBlank()) out += cur.toString()
        return out
    }

    private fun isAllergenStatement(lower: String): Boolean {
        val t = lower.trim()
        if (t.startsWith("allergens") || t.startsWith("allergen:")) return true
        if (!t.startsWith("contains")) return false
        val rest = t.removePrefix("contains").trim().trimStart(':').trim()
        if (rest.isEmpty()) return false
        if (rest.first().isDigit() || rest.startsWith("less") || rest.startsWith("one or more") || rest.startsWith("the following")) return false
        return true
    }

    private fun after(lower: String, starts: List<String>): String {
        for (p in starts) {
            val i = lower.indexOf(p)
            if (i >= 0) return lower.substring(i + p.length)
        }
        return lower
    }

    /** "milk, wheat and soy" -> [milk, wheat, soy]. Drops filler words. */
    fun allergenList(s: String): List<String> = s.replace(Regex("(?i)\\b(that also processes|also processes|processes|that|also|handles|uses|ingredients|products|containing|trace amounts of|traces of|the following|and/or|ingredient|allergens?|statement)\\b"), ",")
        .replace(" and ", ",").replace(" or ", ",").replace("&", ",")
        .split(',', ';', '/', ':').map { TextNorm.normalize(it) }.filter { it.isNotBlank() && it.length < 40 }
}
