package com.baastiklabs.pickless.core.kb

/**
 * Things a person can put on their avoid list. [avoidTags] mean "contains it"; [cautionTags] mean
 * "probably or partly" (soy lecithin, pan-fried, mildly spicy, "vegetable oil" that might be soy).
 * IDs are stored in settings and logs: never rename.
 */
data class AvoidCategory(
    val id: String,
    val label: String,
    val avoidTags: Set<String>,
    val cautionTags: Set<String>,
    val about: String,
    /** Words in a US "Contains:" / "May contain" statement that mean this category. */
    val allergenWords: Set<String> = emptySet(),
    /** Open Food Facts allergen tags that mean this category. */
    val offAllergens: Set<String> = emptySet(),
    /** What "caution" covers, shown next to the strictness setting. */
    val cautionMeans: String = "",
)

object Categories {
    val all: List<AvoidCategory> = listOf(
        AvoidCategory(
            "dairy", "Dairy", setOf("dairy"), setOf("maybe-dairy"),
            "Milk, butter, cheese, cream, yogurt, whey, casein, ghee, milk solids and lactose.",
            setOf("milk", "dairy", "lactose", "whey", "casein"), setOf("en:milk"),
            "flavorings or creamers that may contain milk",
        ),
        AvoidCategory(
            "oat", "Oats", setOf("oat"), emptySet(),
            "Oats, oat flour, oat milk, oat fiber, granola and muesli.",
            setOf("oat", "oats"), setOf("en:oats"),
        ),
        AvoidCategory(
            "soy", "Soy", setOf("soy"), setOf("soy-refined", "maybe-soy"),
            "Soybeans, soy milk, tofu, edamame, miso, soy sauce, soy protein and textured vegetable protein.",
            setOf("soy", "soya", "soybean", "soybeans"), setOf("en:soybeans", "en:soy"),
            "soy lecithin, soybean oil, and \"vegetable oil\" or \"lecithin\" that may be soy",
        ),
        AvoidCategory(
            "fried", "Fried foods", setOf("fried"), setOf("fried-light", "maybe-fried"),
            "Deep-fried food: fries, chips, doughnuts, fried chicken, tempura, anything battered.",
            cautionMeans = "pan-fried food (fried rice, bacon, fried eggs) and snacks that are probably fried",
        ),
        AvoidCategory(
            "spicy", "Spicy foods", setOf("spicy"), setOf("spicy-mild"),
            "Chili, cayenne, jalapeño, hot sauce, sriracha and anything sold as hot or spicy.",
            cautionMeans = "mildly spicy things (curry powder, mild chiles, salsa, \"red pepper\")",
        ),
        AvoidCategory(
            "brussels-sprouts", "Brussels sprouts", setOf("brussels-sprouts"), emptySet(),
            "Brussels sprouts in any form.",
        ),
        AvoidCategory(
            "gluten", "Gluten (wheat, barley, rye)", setOf("gluten"), emptySet(),
            "Wheat, barley, rye, malt, spelt and anything made with them.",
            setOf("wheat", "gluten", "barley", "rye"), setOf("en:gluten"),
        ),
        AvoidCategory(
            "wheat", "Wheat", setOf("wheat"), emptySet(),
            "Wheat flour, bread, pasta, couscous, semolina and spelt.",
            setOf("wheat"), setOf("en:gluten"),
        ),
        AvoidCategory("egg", "Eggs", setOf("egg"), emptySet(), "Eggs, egg whites, mayonnaise, albumen.", setOf("egg", "eggs"), setOf("en:eggs")),
        AvoidCategory("corn", "Corn", setOf("corn"), emptySet(), "Corn, cornmeal, cornstarch, corn syrup, tortillas and corn chips.", setOf("corn")),
        AvoidCategory("peanut", "Peanuts", setOf("peanut"), emptySet(), "Peanuts and peanut butter.", setOf("peanut", "peanuts"), setOf("en:peanuts")),
        AvoidCategory(
            "tree-nut", "Tree nuts", setOf("tree-nut"), emptySet(), "Almonds, cashews, walnuts, pecans, pistachios, hazelnuts and their milks.",
            setOf("tree nut", "tree nuts", "almond", "almonds", "cashew", "cashews", "walnut", "walnuts", "pecan", "pecans", "hazelnut", "hazelnuts", "pistachio", "pistachios", "macadamia", "brazil nut"),
            setOf("en:nuts"),
        ),
        AvoidCategory("sesame", "Sesame", setOf("sesame"), emptySet(), "Sesame seeds, tahini, sesame oil.", setOf("sesame"), setOf("en:sesame-seeds")),
        AvoidCategory("fish", "Fish", setOf("fish"), emptySet(), "Fish, fish sauce, anchovies.", setOf("fish", "anchovy", "anchovies"), setOf("en:fish")),
        AvoidCategory(
            "shellfish", "Shellfish", setOf("shellfish"), emptySet(), "Shrimp, crab, lobster, clams, oysters.",
            setOf("shellfish", "shrimp", "crab", "lobster", "crustacean", "crustaceans"), setOf("en:crustaceans", "en:molluscs"),
        ),
        AvoidCategory("garlic", "Garlic", setOf("garlic"), emptySet(), "Garlic, garlic powder and sauces made with garlic. (Garlic-infused oil is fine.)"),
        AvoidCategory("onion", "Onion", setOf("onion"), emptySet(), "Onion, shallot, leek and onion powder. (Scallion greens and chives are fine.)"),
        AvoidCategory("cruciferous", "Cabbage family", setOf("cruciferous"), emptySet(), "Broccoli, cabbage, cauliflower, kale, Brussels sprouts, radish."),
        AvoidCategory("legume", "Beans & lentils", setOf("legume"), emptySet(), "Beans, lentils, chickpeas, peas and soy."),
        AvoidCategory("nightshade", "Nightshades", setOf("nightshade"), setOf("spicy-mild"), "Tomato, potato, peppers, eggplant, chili and paprika."),
        AvoidCategory("mushroom", "Mushrooms", setOf("mushroom"), emptySet(), "Mushrooms of all kinds."),
        AvoidCategory("citrus", "Citrus", setOf("citrus"), emptySet(), "Oranges, lemons, limes, grapefruit and their juices."),
        AvoidCategory("caffeine", "Caffeine", setOf("caffeine"), emptySet(), "Coffee, tea, cola and energy drinks."),
        AvoidCategory("alcohol", "Alcohol", setOf("alcohol"), emptySet(), "Beer, wine, spirits and cocktails."),
        AvoidCategory("polyol", "Sugar alcohols", setOf("polyol"), emptySet(), "Sorbitol, mannitol, xylitol, maltitol, isomalt (sugar-free sweets and gum)."),
        AvoidCategory("added-sugar", "Added sugar", setOf("added-sugar"), emptySet(), "Sugar, syrups, honey and sweets."),
    )

    private val byId = all.associateBy { it.id }

    fun byId(id: String): AvoidCategory? = byId[id]

    /** What Pickless starts with for its owner: their elimination list. */
    val defaultAvoidList = listOf("dairy", "oat", "soy", "fried", "spicy", "brussels-sprouts")
}

/** How strictly to treat the "caution" tier and label warnings for one avoid item. */
enum class Strictness { RELAXED, STANDARD, STRICT }

/** What a "may contain" / shared-equipment warning counts as. */
enum class TracePolicy { IGNORE, CAUTION, AVOID }

/** Where an avoided food stands in the elimination diet. IDs are stored: never rename. */
enum class AvoidStatus(val id: String, val label: String, val flags: Boolean) {
    AVOIDING("avoiding", "Avoiding", true),
    TESTING("testing", "Testing now", true),
    TRIGGER("trigger", "Confirmed trigger", true),
    SMALL_OK("small-ok", "OK in small amounts", true),
    TOLERATED("tolerated", "Tolerated", false);

    companion object {
        fun byId(id: String): AvoidStatus = entries.firstOrNull { it.id == id } ?: AVOIDING
    }
}

/**
 * One line of the avoid list. Either a built-in [categoryId] or a custom food ([foodId], which also
 * covers foods made from it) or a custom word ([term], matched in ingredient lists).
 */
data class AvoidItem(
    val key: String,
    val label: String,
    val categoryId: String? = null,
    val foodId: String? = null,
    val term: String? = null,
    val status: AvoidStatus = AvoidStatus.AVOIDING,
    val strictness: Strictness = Strictness.STANDARD,
    val traces: TracePolicy = TracePolicy.CAUTION,
    val sinceEpochDay: Long? = null,
) {
    val category: AvoidCategory? get() = categoryId?.let(Categories::byId)

    companion object {
        fun ofCategory(id: String, sinceEpochDay: Long? = null): AvoidItem {
            val c = Categories.byId(id) ?: error("unknown category $id")
            return AvoidItem(key = "cat:$id", label = c.label, categoryId = id, sinceEpochDay = sinceEpochDay)
        }

        fun ofFood(entry: FoodEntry, sinceEpochDay: Long? = null) =
            AvoidItem(key = "food:${entry.id}", label = entry.name, foodId = entry.id, sinceEpochDay = sinceEpochDay)

        fun ofTerm(term: String, sinceEpochDay: Long? = null) =
            AvoidItem(key = "term:${term.trim().lowercase()}", label = term.trim(), term = term.trim(), sinceEpochDay = sinceEpochDay)
    }
}
