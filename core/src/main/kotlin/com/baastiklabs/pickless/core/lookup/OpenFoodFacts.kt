package com.baastiklabs.pickless.core.lookup

import com.baastiklabs.pickless.core.analysis.ProductInput
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.int
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/** Open Food Facts API v2: URLs to call and how to read the answers. Network code lives in the app. */
object OpenFoodFacts {
    const val HOST = "https://world.openfoodfacts.org"
    const val FIELDS = "code,product_name,product_name_en,generic_name_en,brands,ingredients_text,ingredients_text_en," +
        "allergens_tags,traces_tags,categories_tags,labels_tags,serving_size,serving_quantity,nutriments,quantity"

    /** No account or key; OFF asks apps to identify themselves in the User-Agent. */
    fun userAgent(version: String) = "Pickless/$version (Android; personal diet app)"

    fun productUrl(code: String) = "$HOST/api/v2/product/$code.json?fields=$FIELDS"

    fun searchUrl(query: String, pageSize: Int = 20): String {
        val q = java.net.URLEncoder.encode(query, "UTF-8")
        return "$HOST/cgi/search.pl?search_terms=$q&search_simple=1&action=process&json=1&page_size=$pageSize&fields=$FIELDS"
    }

    private val json = Json { ignoreUnknownKeys = true; isLenient = true }

    /** Null when the product isn't in the database. */
    fun parseProduct(body: String): ProductInput? {
        val root = runCatching { json.parseToJsonElement(body).jsonObject }.getOrNull() ?: return null
        val status = (root["status"] as? JsonPrimitive)?.intOrNull ?: 0
        if (status != 1) return null
        val p = root["product"] as? JsonObject ?: return null
        return toInput(p)
    }

    fun parseSearch(body: String): List<ProductInput> {
        val root = runCatching { json.parseToJsonElement(body).jsonObject }.getOrNull() ?: return emptyList()
        val arr = root["products"]?.let { runCatching { it.jsonArray }.getOrNull() } ?: return emptyList()
        return arr.mapNotNull { (it as? JsonObject)?.let(::toInput) }.filter { !it.name.isNullOrBlank() }
    }

    private fun toInput(p: JsonObject): ProductInput {
        fun str(k: String) = (p[k] as? JsonPrimitive)?.contentOrNull?.trim()?.takeIf { it.isNotEmpty() }
        fun list(k: String) = p[k]?.let { e -> runCatching { e.jsonArray.mapNotNull { it.jsonPrimitive.contentOrNull } }.getOrNull() } ?: emptyList()
        val n = p["nutriments"] as? JsonObject
        fun nut(k: String): Double? = n?.get(k)?.let { e -> (e as? JsonPrimitive)?.let { it.doubleOrNull ?: it.contentOrNull?.toDoubleOrNull() } }
        val servingQty = (p["serving_quantity"] as? JsonPrimitive)?.let { it.doubleOrNull ?: it.contentOrNull?.toDoubleOrNull() }
        return ProductInput(
            code = str("code"),
            name = str("product_name_en") ?: str("product_name") ?: str("generic_name_en"),
            brand = str("brands")?.split(',')?.firstOrNull()?.trim(),
            ingredientsText = str("ingredients_text_en") ?: str("ingredients_text"),
            categories = list("categories_tags"),
            labels = list("labels_tags"),
            allergens = list("allergens_tags"),
            traces = list("traces_tags"),
            servingGrams = servingQty?.takeIf { it > 0 } ?: str("serving_size")?.let(Units::gramsIn),
            servingLabel = str("serving_size"),
            fatPer100 = nut("fat_100g"),
            sugarsPer100 = nut("sugars_100g"),
            fiberPer100 = nut("fiber_100g"),
            polyolsPer100 = nut("polyols_100g"),
            source = "openfoodfacts",
        )
    }

    @Suppress("unused")
    private fun JsonElement.intValue(): Int? = runCatching { jsonPrimitive.int }.getOrNull()
}

/** Reads amounts like "1 cup (240 mL)", "28g", "2 tbsp (32 g)". */
object Units {
    private val grams = Regex("(\\d+(?:\\.\\d+)?)\\s*(g|gr|grams?|ml|mL|milliliters?)\\b", RegexOption.IGNORE_CASE)
    private val ounces = Regex("(\\d+(?:\\.\\d+)?)\\s*(oz|ounces?)\\b", RegexOption.IGNORE_CASE)
    private val household = Regex("(\\d+(?:\\.\\d+)?|\\d+/\\d+|½|¼|¾|⅓|⅔)\\s*(cups?|tbsp|tablespoons?|tsp|teaspoons?)\\b", RegexOption.IGNORE_CASE)

    fun gramsIn(text: String): Double? {
        grams.find(text)?.let { return it.groupValues[1].toDouble() }
        ounces.find(text)?.let { return it.groupValues[1].toDouble() * 28.35 }
        household.find(text)?.let { m ->
            val n = number(m.groupValues[1]) ?: return null
            val unit = m.groupValues[2].lowercase()
            return n * when {
                unit.startsWith("cup") -> 240.0
                unit.startsWith("tbsp") || unit.startsWith("tablespoon") -> 15.0
                else -> 5.0
            }
        }
        return null
    }

    fun number(s: String): Double? = when (s) {
        "½" -> 0.5; "¼" -> 0.25; "¾" -> 0.75; "⅓" -> 1.0 / 3; "⅔" -> 2.0 / 3
        else -> if (s.contains('/')) s.split('/').let { (a, b) -> a.toDoubleOrNull()?.let { x -> b.toDoubleOrNull()?.takeIf { it != 0.0 }?.let { x / it } } } else s.toDoubleOrNull()
    }
}
