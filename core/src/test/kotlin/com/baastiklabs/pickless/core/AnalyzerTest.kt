package com.baastiklabs.pickless.core

import com.baastiklabs.pickless.core.analysis.Analyzer
import com.baastiklabs.pickless.core.analysis.HitLevel
import com.baastiklabs.pickless.core.analysis.ProductInput
import com.baastiklabs.pickless.core.analysis.Verdict
import com.baastiklabs.pickless.core.fodmap.FodmapLevel
import com.baastiklabs.pickless.core.kb.AvoidItem
import com.baastiklabs.pickless.core.kb.Categories
import com.baastiklabs.pickless.core.kb.KnowledgeBase
import com.baastiklabs.pickless.core.kb.Strictness
import com.baastiklabs.pickless.core.label.IngredientParser
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

class AnalyzerTest {
    private val kb = KnowledgeBase.load()
    private val analyzer = Analyzer(kb)
    private val avoid = Categories.defaultAvoidList.map { AvoidItem.ofCategory(it) }
    private val ctx = Analyzer.Context(avoid)

    private fun product(name: String, ingredients: String, cats: List<String> = emptyList(), fat: Double? = null) =
        analyzer.analyzeProduct(ProductInput(name = name, ingredientsText = ingredients, categories = cats, fatPer100 = fat, servingGrams = 240.0), ctx)

    private fun levelOf(a: com.baastiklabs.pickless.core.analysis.Analysis, cat: String) = a.findings.firstOrNull { it.item.categoryId == cat }?.level

    @Test fun milkOrderingAtOneCup() {
        fun score(id: String) = analyzer.analyzeEntry(kb.require(id), ctx).fodmapAt(240.0).score
        val almond = score("almond-milk")
        val soyProtein = score("soy-milk-protein")
        val oat = score("oat-milk")
        val soyWhole = score("soy-milk-whole")
        assertTrue(almond < soyProtein && soyProtein < oat && oat < soyWhole, "almond=$almond soyProtein=$soyProtein oat=$oat soyWhole=$soyWhole")
        assertEquals(FodmapLevel.LOW, analyzer.analyzeEntry(kb.require("almond-milk"), ctx).fodmapAt(240.0).level)
        assertEquals(FodmapLevel.MODERATE, analyzer.analyzeEntry(kb.require("oat-milk"), ctx).fodmapAt(240.0).level)
        assertEquals(FodmapLevel.HIGH, analyzer.analyzeEntry(kb.require("soy-milk-whole"), ctx).fodmapAt(240.0).level)
        assertEquals(FodmapLevel.LOW, analyzer.analyzeEntry(kb.require("oat-milk"), ctx).fodmapAt(120.0).level)
    }

    @Test fun parsesUsLabel() {
        val p = IngredientParser.parse("INGREDIENTS: Water, Sugar, Enriched Flour (Wheat Flour, Niacin, Reduced Iron), Contains 2% or less of: Salt, Soy Lecithin, Natural Flavor. CONTAINS: MILK, WHEAT, SOY. May contain peanuts.")
        assertEquals(listOf("water", "sugar", "enriched flour", "salt", "soy lecithin", "natural flavor"), p.ingredients.map { it.text.lowercase() })
        assertTrue(p.ingredients[3].minor && !p.ingredients[2].minor)
        assertEquals(3, p.ingredients[2].children.size)
        assertEquals(listOf("milk", "wheat", "soy"), p.contains)
        assertEquals(listOf("peanuts"), p.mayContain)
    }

    @Test fun almondMilkIsGood() {
        val a = product("Unsweetened Almondmilk", "Almondmilk (Filtered Water, Almonds), Calcium Carbonate, Sea Salt, Potassium Citrate, Sunflower Lecithin, Gellan Gum, Vitamin A Palmitate, Vitamin D2, D-Alpha-Tocopherol (Natural Vitamin E).")
        assertTrue(a.findings.isEmpty(), a.findings.toString())
        assertEquals(Verdict.GOOD, a.verdict, a.verdictReasons.toString())
        assertEquals("almond-milk", a.identityId)
    }

    @Test fun oatMilkHitsOats() {
        val a = product("Oatmilk Original", "Oat base (water, oats), low erucic acid rapeseed oil, dipotassium phosphate, calcium carbonate, tricalcium phosphate, sea salt, dicalcium phosphate, riboflavin, vitamin A, vitamin D2, vitamin B12.")
        assertEquals(HitLevel.AVOID, levelOf(a, "oat"))
        assertEquals(Verdict.AVOID, a.verdict)
        assertEquals("oat-milk", a.identityId)
    }

    @Test fun soyMilkVariantsFollowIngredients() {
        assertEquals("soy-milk-whole", product("Organic Soymilk", "Filtered water, whole organic soybeans, cane sugar, sea salt").identityId)
        assertEquals("soy-milk-protein", product("Soy Milk", "Water, soy protein isolate, sugar, sunflower oil, salt").identityId)
    }

    @Test fun refinedSoyIsCautionNotAvoid() {
        val a = product("Sandwich Cookies", "Unbleached enriched flour (wheat flour, niacin, reduced iron), sugar, palm oil, soybean oil, cocoa, salt, soy lecithin, vanillin")
        assertEquals(HitLevel.CAUTION, levelOf(a, "soy"), a.findings.toString())
        val strict = analyzer.analyzeProduct(ProductInput(name = "Cookies", ingredientsText = "flour, sugar, soy lecithin"),
            Analyzer.Context(listOf(AvoidItem.ofCategory("soy").copy(strictness = Strictness.STRICT))))
        assertEquals(HitLevel.AVOID, strict.findings.single().level)
        val parenthesis = product("Crackers", "Wheat flour, vegetable oil (soybean and/or canola oil), salt")
        assertEquals(HitLevel.CAUTION, levelOf(parenthesis, "soy"), parenthesis.findings.toString())
        val unnamed = product("Crackers", "Wheat flour, vegetable oil, salt")
        assertEquals(HitLevel.CAUTION, levelOf(unnamed, "soy"))
        val named = product("Crackers", "Wheat flour, vegetable oil (sunflower, canola), salt")
        assertNull(levelOf(named, "soy"), named.findings.toString())
        val tofu = product("Firm Tofu", "Water, soybeans, calcium sulfate")
        assertEquals(HitLevel.AVOID, levelOf(tofu, "soy"))
    }

    @Test fun dairyHiddenNames() {
        for (ing in listOf("whey", "sodium caseinate", "milk solids", "ghee", "butter", "nonfat dry milk", "cream", "lactose", "milk protein concentrate", "buttermilk", "goat cheese", "parmesan cheese")) {
            val a = product("Test", "rice, $ing, salt")
            assertEquals(HitLevel.AVOID, levelOf(a, "dairy"), "$ing should be dairy: ${a.findings}")
        }
        for (ing in listOf("cocoa butter", "peanut butter", "lactic acid", "cream of tartar", "coconut cream", "coconut milk", "shea butter", "calcium lactate", "butternut squash")) {
            val a = product("Test", "rice, $ing, salt")
            assertNull(levelOf(a, "dairy"), "$ing is not dairy: ${a.findings}")
        }
        val statement = product("Test", "rice, natural flavor. Contains: Milk.")
        assertEquals(HitLevel.AVOID, levelOf(statement, "dairy"))
        val trace = product("Test", "rice, salt. May contain milk.")
        assertEquals(HitLevel.TRACE, levelOf(trace, "dairy"))
    }

    @Test fun wordsInsideOtherWordsDontMatch() {
        assertNull(levelOf(product("Test", "goat milk, salt"), "oat"))
        assertNull(levelOf(product("Test", "buckwheat, coconut flour, salt"), "oat"))
        val buckwheat = product("Test", "buckwheat flour, coconut flour")
        assertTrue(buckwheat.features.ingredients.none { it == "wheat-flour" }, buckwheat.features.ingredients.toString())
    }

    @Test fun friedAndSpicyFromNamesAndLabels() {
        val chips = product("Classic Potato Chips", "Potatoes, vegetable oil (sunflower, corn, and/or canola oil), salt")
        assertEquals(HitLevel.AVOID, levelOf(chips, "fried"))
        assertNull(levelOf(chips, "soy"), chips.findings.toString())
        val baked = product("Baked Potato Crisps", "dried potatoes, corn starch, sugar, corn oil, salt")
        assertEquals(HitLevel.CAUTION, levelOf(baked, "fried"))
        val hot = product("Spicy Nacho Tortilla Chips", "corn, vegetable oil, cheddar cheese, salt, spices, onion powder")
        assertEquals(HitLevel.AVOID, levelOf(hot, "spicy"))
        val hotDog = product("Beef Hot Dogs", "beef, water, salt, spices, paprika")
        assertNull(levelOf(hotDog, "spicy"), hotDog.findings.toString())
        val jalapeno = product("Salsa", "tomatoes, onions, jalapeno peppers, salt")
        assertEquals(HitLevel.AVOID, levelOf(jalapeno, "spicy"))
        val label = product("Snack", "potatoes fried in sunflower oil, salt")
        assertEquals(HitLevel.AVOID, levelOf(label, "fried"))
    }

    @Test fun typedFoodsAndDishes() {
        val sprouts = analyzer.analyzeEntry(kb.require("brussels-sprouts"), ctx)
        assertEquals(Verdict.AVOID, sprouts.verdict)
        val fries = analyzer.analyzeEntry(kb.require("french-fries"), ctx)
        assertEquals(HitLevel.AVOID, levelOf(fries, "fried"))
        val latte = analyzer.analyzeEntry(kb.require("latte"), ctx)
        assertEquals(HitLevel.AVOID, levelOf(latte, "dairy"))
        val pizza = analyzer.analyzeEntry(kb.require("pizza"), ctx)
        assertTrue("garlic" in pizza.features.tags, pizza.features.tags.toString())
        val rice = analyzer.analyzeEntry(kb.require("chicken-rice"), ctx)
        assertEquals(Verdict.GOOD, rice.verdict, rice.verdictReasons.toString())
    }

    @Test fun honeyAndInulinDriveFodmapInProducts() {
        val bar = product("Fiber Bar", "Chicory root fiber, almonds, honey, rice crisps (rice flour, sugar), salt")
        val at = bar.fodmapAt(40.0)
        assertTrue(at.score >= 6.0, "inulin-first bar should be high: ${at.loads}")
        val plain = product("Rice Cakes", "whole grain brown rice, salt")
        assertEquals(FodmapLevel.LOW, plain.fodmapAt(20.0).level)
    }

    @Test fun mealStacking() {
        val p = kb.profile(kb.require("broccoli"))
        val one = analyzer.meal(listOf(p to 75.0))
        val three = analyzer.meal(listOf(p to 75.0, kb.profile(kb.require("asparagus")) to 12.0, kb.profile(kb.require("grapes")) to 150.0))
        assertTrue(one.score < 3.0)
        assertTrue(three.score > one.score)
    }

    @Test fun guessUnknownFoods() {
        val g = analyzer.guess("kohlrabi and garlic slaw", ctx)!!
        assertTrue("garlic" in g.features.ingredients)
        assertNull(analyzer.guess("zzzz qqq", ctx))
        assertTrue(analyzer.guess("mystery beans", ctx) != null)
    }
}
