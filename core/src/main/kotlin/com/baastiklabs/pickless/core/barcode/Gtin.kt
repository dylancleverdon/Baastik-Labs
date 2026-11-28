package com.baastiklabs.pickless.core.barcode

/**
 * Barcode helpers. Products are stored under a 14-digit GTIN key so UPC-A (12), EAN-13 and
 * GTIN-14 spellings of the same code meet; UPC-E (8, starting 0/1) is expanded first.
 */
object Gtin {
    fun digits(raw: String): String = raw.filter { it.isDigit() }

    fun checkDigitOk(code: String): Boolean {
        if (code.length !in setOf(8, 12, 13, 14) || code.any { !it.isDigit() }) return false
        val body = code.dropLast(1)
        var sum = 0
        for ((i, c) in body.reversed().withIndex()) sum += (c - '0') * if (i % 2 == 0) 3 else 1
        return (10 - sum % 10) % 10 == code.last() - '0'
    }

    /** UPC-E (with number system and check digit, 8 digits) to UPC-A. */
    fun upcEToUpcA(upce: String): String? {
        if (upce.length != 8 || upce[0] !in "01") return null
        val ns = upce[0]
        val d = upce.substring(1, 7)
        val check = upce[7]
        val body = when (d[5]) {
            '0', '1', '2' -> "${d[0]}${d[1]}${d[5]}0000${d[2]}${d[3]}${d[4]}"
            '3' -> "${d[0]}${d[1]}${d[2]}00000${d[3]}${d[4]}"
            '4' -> "${d[0]}${d[1]}${d[2]}${d[3]}00000${d[4]}"
            else -> "${d[0]}${d[1]}${d[2]}${d[3]}${d[4]}0000${d[5]}"
        }
        val a = "$ns$body$check"
        return if (checkDigitOk(a)) a else null
    }

    /** Canonical 14-digit key, or null when it isn't a valid product barcode. */
    fun key(raw: String): String? {
        var c = digits(raw)
        if (c.length == 8 && c[0] in "01") upcEToUpcA(c)?.let { c = it } // UPC-E is far more common in the US than EAN-8
        if (!checkDigitOk(c)) return null
        return c.padStart(14, '0')
    }

    /** Spellings to try against databases: EAN-13, UPC-A, GTIN-14, EAN-8. */
    fun lookupCodes(key: String): List<String> {
        val k = key.padStart(14, '0')
        val out = linkedSetOf<String>()
        val ean13 = k.takeLast(13)
        out += ean13
        if (ean13.startsWith("0")) out += ean13.drop(1)
        out += k
        if (k.startsWith("000000")) out += k.takeLast(8)
        return out.toList()
    }

    /** What to show: UPC-A for US products, otherwise EAN-13. */
    fun display(key: String): String {
        val ean13 = key.padStart(14, '0').takeLast(13)
        return if (ean13.startsWith("0")) ean13.drop(1) else ean13
    }
}
