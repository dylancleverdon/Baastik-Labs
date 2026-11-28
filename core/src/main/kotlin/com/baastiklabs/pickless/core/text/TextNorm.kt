package com.baastiklabs.pickless.core.text

import java.text.Normalizer

/**
 * Text normalization shared by the label matcher and search, so an alias written in the knowledge
 * base matches the same words on a label: lowercase, no accents or apostrophes, "&" becomes "and",
 * other punctuation becomes a space, and each word is reduced to a crude singular ([stem]).
 */
object TextNorm {
    private val marks = Regex("\\p{Mn}+")

    fun normalize(input: String): String {
        val lower = Normalizer.normalize(input.lowercase(), Normalizer.Form.NFD).replace(marks, "")
        val sb = StringBuilder(lower.length + 8)
        for (i in lower.indices) {
            val c = lower[i]
            when {
                c == '\'' || c == '’' || c == '‘' || c == '`' -> {}
                c == '&' -> sb.append(" and ")
                c.isLetterOrDigit() || c == '%' -> sb.append(c)
                c == '.' && i > 0 && i + 1 < lower.length && lower[i - 1].isDigit() && lower[i + 1].isDigit() -> sb.append(c)
                else -> sb.append(' ')
            }
        }
        return sb.toString().trim().replace(Regex("\\s+"), " ")
    }

    fun words(input: String): List<String> = normalize(input).split(' ').filter { it.isNotEmpty() }

    /** Words reduced with [stem], for plural-insensitive matching. */
    fun stems(input: String): List<String> = words(input).map(::stem)

    /** A deliberately simple singular: both sides of every comparison use it, so it only has to be consistent. */
    fun stem(word: String): String {
        if (word.length <= 3 || word.any { it.isDigit() }) return word
        return when {
            word.endsWith("ies") && word.length > 4 -> word.dropLast(3) + "y"
            word.endsWith("oes") -> word.dropLast(2)
            word.endsWith("ches") || word.endsWith("shes") || word.endsWith("sses") || word.endsWith("xes") -> word.dropLast(2)
            word.endsWith("ss") || word.endsWith("us") || word.endsWith("is") -> word
            word.endsWith("s") -> word.dropLast(1)
            else -> word
        }
    }
}
