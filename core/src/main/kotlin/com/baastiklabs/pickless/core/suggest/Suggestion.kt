package com.baastiklabs.pickless.core.suggest

import kotlinx.serialization.Serializable

/**
 * What the suggestion box posts to the sheet's Apps Script (suggestions/Code.gs). Every suggestion
 * has an id; the script ignores ids it already has, so a retry never makes a duplicate row.
 */
@Serializable
data class SuggestionPayload(
    val id: String,
    val kind: String,
    val text: String,
    val from: String,
    val version: String,
    val phone: String,
    val android: String,
    val at: String,
    val log: String? = null,
)

object SuggestionKinds {
    val all = listOf("Idea", "Bug", "Wrong food info", "Other")
}
