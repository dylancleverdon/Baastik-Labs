package com.baastiklabs.pickless.core

import com.baastiklabs.pickless.core.barcode.Gtin
import com.baastiklabs.pickless.core.fodmap.FodmapGroup
import com.baastiklabs.pickless.core.lookup.BraveFodmap
import com.baastiklabs.pickless.core.lookup.OpenFoodFacts
import com.baastiklabs.pickless.core.lookup.Units
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

class LookupTest {
    @Test fun barcodes() {
        assertEquals("00012000161155", Gtin.key("012000161155"))      // UPC-A
        assertEquals("00012000161155", Gtin.key("0012000161155"))     // same as EAN-13
        assertNull(Gtin.key("012000161156"))                          // bad check digit
        assertEquals("042100005264", Gtin.upcEToUpcA("04252614"))
        assertEquals("012000161155", Gtin.display("00012000161155"))
        assertTrue("0012000161155" in Gtin.lookupCodes("00012000161155"))
        assertTrue("012000161155" in Gtin.lookupCodes("00012000161155"))
        assertEquals("05000159407236".takeLast(13), Gtin.lookupCodes(Gtin.key("5000159407236")!!).first())
    }

    @Test fun units() {
        assertEquals(240.0, Units.gramsIn("1 cup (240 mL)"))
        assertEquals(28.0, Units.gramsIn("28g"))
        assertEquals(30.0, Units.gramsIn("2 tbsp"))
        assertEquals(56.7, Units.gramsIn("2 oz")!!, 0.01)
    }

    @Test fun readsOpenFoodFacts() {
        val body = """{"code":"0025293600232","status":1,"product":{"code":"0025293600232","product_name":"Almondmilk Original",
            "brands":"Silk, WhiteWave","ingredients_text":"Almondmilk (filtered water, almonds), cane sugar, sea salt, sunflower lecithin",
            "allergens_tags":["en:nuts"],"traces_tags":[],"categories_tags":["en:plant-based-foods","en:almond-milks"],"labels_tags":[],
            "serving_size":"1 cup (240 ml)","serving_quantity":240,"nutriments":{"fat_100g":1.04,"sugars_100g":3.33,"fiber_100g":0}}}"""
        val p = OpenFoodFacts.parseProduct(body)!!
        assertEquals("Almondmilk Original", p.name)
        assertEquals("Silk", p.brand)
        assertEquals(240.0, p.servingGrams)
        assertEquals(3.33, p.sugarsPer100)
        assertNull(OpenFoodFacts.parseProduct("""{"status":0,"status_verbose":"product not found"}"""))
    }

    @Test fun readsBraveSnippets() {
        val body = """{"web":{"results":[
          {"title":"Is Kohlrabi Low FODMAP?","url":"https://www.monashfodmap.com/blog/kohlrabi","meta_url":{"hostname":"www.monashfodmap.com"},
           "description":"Kohlrabi is <strong>low FODMAP</strong> in servings of 1/2 cup (75g).","extra_snippets":["Larger servings contain moderate amounts of fructans."]},
          {"title":"Kohlrabi FODMAP","url":"https://www.fodmapeveryday.com/kohlrabi","description":"Good news: kohlrabi is considered low FODMAP. A low FODMAP serving is 75 g."}
        ]}}"""
        val r = BraveFodmap.parse("kohlrabi", body)
        assertTrue(r.verdict == BraveFodmap.WebVerdict.LOW || r.verdict == BraveFodmap.WebVerdict.MIXED, r.toString())
        assertEquals(75.0, r.lowServingGrams)
        assertTrue(r.sources.first().host == "monashfodmap.com")
        val high = BraveFodmap.parse("cashews", """{"web":{"results":[{"title":"Cashews","url":"https://a.com/x","description":"Cashews are high FODMAP because they contain GOS and fructans."}]}}""")
        assertEquals(BraveFodmap.WebVerdict.HIGH, high.verdict)
        assertTrue(FodmapGroup.GOS in high.groups && FodmapGroup.FRUCTANS in high.groups, high.groups.toString())
        assertEquals(BraveFodmap.WebVerdict.UNKNOWN, BraveFodmap.parse("x", "{}").verdict)
    }
}
