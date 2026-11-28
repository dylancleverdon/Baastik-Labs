package com.baastiklabs.pickless.core

import com.baastiklabs.pickless.core.kb.KnowledgeBase
import kotlin.test.Test
import kotlin.test.assertTrue
import kotlin.test.fail

class KnowledgeBaseTest {
    private val kb = KnowledgeBase.load()

    @Test fun hasSeveralHundredEntries() {
        assertTrue(kb.entries.size > 400, "only ${kb.entries.size} entries")
    }

    @Test fun aliasesAreUnambiguous() {
        val conflicts = kb.aliasOwners.filter { it.value.size > 1 }
        if (conflicts.isNotEmpty()) fail("aliases claimed by more than one food:\n" + conflicts.entries.joinToString("\n") { "${it.key} -> ${it.value}" })
    }

    @Test fun parentsAndComponentsExist() {
        val problems = mutableListOf<String>()
        for (e in kb.entries.values) {
            e.parentId?.let { if (kb[it] == null) problems += "${e.id}: missing parent $it" }
            e.components.forEach { if (kb[it.id] == null) problems += "${e.id}: missing component ${it.id}" }
            if (e.kind.name == "DISH" && e.components.isEmpty()) problems += "${e.id}: dish without components"
        }
        if (problems.isNotEmpty()) fail(problems.joinToString("\n"))
    }

    @Test fun searchFindsCommonFoods() {
        fun top(q: String, n: Int = 3) = kb.search(q).take(n).map { it.entry.id }
        assertTrue("oat-milk" in top("oat milk", 1), top("oat milk").toString())
        assertTrue(top("soy milk").containsAll(listOf("soy-milk-whole", "soy-milk-protein")), top("soy milk").toString())
        assertTrue("brussels-sprouts" in top("brussel sprouts", 1), top("brussel sprouts").toString())
        assertTrue("brussels-sprouts" in top("brusels sprouts"), top("brusels sprouts").toString())
        assertTrue("almond-milk" in top("almond milk", 1), top("almond milk").toString())
        assertTrue("pad-thai" in top("pad thai", 1), top("pad thai").toString())
        assertTrue("banana" in top("banana"), top("banana").toString())
    }
}
