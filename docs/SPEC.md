# Pickless feature spec

Everything agreed in the planning conversation, and where it lives.

## Check a food
- [x] Barcode scan (camera, offline decoding): phone cache → offline USDA US product pack (~427k
      products, downloaded on Wi-Fi) → Open Food Facts
- [x] Type a food or dish: ~470 built-in foods, label ingredients and typical dishes; fuzzy search;
      ambiguous names show variants (soy milk: whole-bean vs soy-protein)
- [x] Snap the label: on-device text recognition, editable, remembered for that barcode
- [x] Educated guess for unknown typed foods; Brave Search lookup of FODMAP sites with sources shown,
      capped per month
- [x] Result: verdict (good / caution / avoid), avoid-list evidence with the label's own words,
      FODMAP 0-10 per serving with a slider and safe amount, per-type breakdown and what drives it,
      ingredient list with highlights, notes, confidence and source, your history with the food,
      favorite, fine-for-me / bad-for-me, correct the FODMAP rating, report wrong info

## Avoid list
- [x] Categories with hidden names (dairy, oats, soy, fried, spicy, Brussels sprouts, gluten, wheat,
      egg, corn, nuts, sesame, fish, shellfish, garlic, onion, cabbage family, legumes, nightshades,
      mushrooms, citrus, caffeine, alcohol, sugar alcohols, added sugar) plus custom foods and words
- [x] Strictness per item (soy lecithin / soybean oil = caution by default), "may contain" policy
- [x] Status over time: avoiding → testing → tolerated / OK in small amounts / trigger
- [x] US "Contains:" and "May contain" statements read from labels

## FODMAP
- [x] Built-in table (estimates from published research, never copied from an app)
- [x] Packaged foods estimated from ingredient order, percentages, "2% or less", the nutrition panel
      and certified-low-FODMAP logos; whole-product identities for milks, breads, cheese, sauces
- [x] Meal stacking warning; personal score after FODMAP tests

## Logging
- [x] Food with portion, time, meal; saved meals and recipes
- [x] BMs: Bristol 1-7 drawings, optional urgency, pain, straining, accident, blood, color; red-flag advice
- [x] Symptoms with severity; evening check-in (overall, stress, sleep, nicotine usual/more/less/none,
      alcohol, caffeine, exercise, period if enabled, meds, sick/travel days excluded, no-BM days)
- [x] Weekly IBS severity check-in (0-500); after-meal nudges; quick buttons (coffee, alcohol, water, extra pouch)

## Trigger detective
- [x] Starts from the first log, with an early-read banner until there are ~2 weeks of mostly
      complete logs, 20 rated BMs, a few bad ones, and stable top suspects
- [x] Bayesian comparison with shrinkage and false-discovery control; untangles foods eaten together;
      "explains" narrower foods; lifestyle factors (sleep, stress, nicotine, alcohol, caffeine,
      exercise, period); symptom links within 8 hours; safe foods; BM pattern; next best test
- [x] "Why was that one bad?" for any BM
- [x] Tested on simulated diaries with planted triggers and with none

## Coach
- [x] Guided 3-day tests with washout for every avoid item, tailored steps (dairy: lactose-free milk →
      hard cheese → milk; soy: firm tofu → whole-bean soy milk), FODMAP group tests
- [x] Readiness (time avoided, calm stretch), suggestion of what to test next, reactions read from
      the diary, contamination warnings, results update the avoid list and personal FODMAP score

## Reports and everyday
- [x] PDF report for a doctor or dietitian; CSV export; JSON backup and restore; Android backup
- [x] Restaurant card; shopping mode (buzz patterns); home-screen widget; fingerprint lock
- [x] Optional Health Connect (sleep, exercise, period)

## Infrastructure
- [x] Auto-updater from this repo's `pickless-*` releases (SHA-256 and signature checked)
- [x] Suggestion box to "elimination diet app suggestion sheet" with Send logs, QR connect
- [x] CI: tests, lint, signed APK, prerelease + rolling channel manifest; monthly-ish USDA pack rebuild
