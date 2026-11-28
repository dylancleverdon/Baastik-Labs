# Baastik Labs repo: Pickless

This repository holds more than one program, each on its own long-lived branch with unrelated
history (`norg`, `tutti`, `cymatica`, `text-to-synth`, ...). **Pickless** (an Android elimination-diet
helper: scan or type a food, check it against an avoid list and FODMAP, log BMs, find triggers)
lives only on the `pickless` branch.

## Branch policy (important)
- Pickless's code lives on `pickless`. Never merge it into another program's branch or `main`, and
  never touch other programs' branches from a Pickless session.
- Work on your `claude/*` session branch, then push it to `pickless` to ship.
- **Every push to `pickless` becomes an update** on the owner's phone: `.github/workflows/pickless.yml`
  builds, tests and publishes `pickless-v<version>` plus the rolling `pickless-channel` release
  (its `manifest.json` is what the in-app updater reads). Installed copies pick it up within an hour.
  Run the tests before pushing; the release run publishes only if they pass. Docs-only pushes publish nothing.
- Releases are prerelease-only and never marked "Latest", so other programs' releases are unaffected.

## Build and test
```
./gradlew :core:test                 # all logic: knowledge base, analyzer, detective, coach (fast, JVM)
./gradlew :app:testDebugUnitTest     # app tests (Robolectric)
./gradlew :app:assembleRelease       # signed APK in app/build/outputs/apk/release/
python3 -m pytest tools/usda-pack -q # the USDA product pack builder
```
- Needs JDK 17+ and the Android SDK (platform 36, build-tools 36). Put `sdk.dir=...` in `local.properties`.
- `settings.gradle.kts` resolves Maven Central through Google's mirror first: plain Maven Central
  rate-limits shared build machines.

## Architecture
- `core/` is plain Kotlin (no Android) and holds all the logic, so it's tested on the JVM:
  - `kb/` the knowledge base: `src/main/resources/pickless/foods.txt` (foods, label ingredients,
    dishes, hidden names, FODMAP thresholds). **IDs are permanent**: logs refer to them. Aliases must
    be unambiguous (a test fails if two foods claim the same alias). FODMAP numbers are estimates from
    published research and dietitian guidance; never copy them from an app's database.
  - `label/IngredientParser` reads US labels; `analysis/Analyzer` turns foods/products into a verdict
    against the avoid list plus a FODMAP profile (`fodmap/FodmapProfile`, linear in grams, stacks per meal).
  - `detective/` finds triggers from the diary (Bayesian, with false-discovery control and untangling).
    `DetectiveTest` checks it against simulated diaries with planted triggers and with none: keep both passing.
  - `coach/` reintroduction tests; `update/` manifest rules; `lookup/` Open Food Facts, Brave, barcodes.
- `app/` is the Android app (Kotlin, Jetpack Compose, Room). No allocation-heavy work on the main thread;
  network calls only to Open Food Facts, this repo's releases, Brave (only with the owner's key, capped
  monthly) and the suggestion sheet's Apps Script.
- `tools/usda-pack/` builds the offline US product pack from USDA FoodData Central (public domain),
  published by `.github/workflows/pickless-data.yml` to the `pickless-data` release.
- `suggestions/` holds the suggestion box's Apps Script (`Code.gs`) for the owner's
  "elimination diet app suggestion sheet".

## Rules
- The owner never does anything by hand beyond the one-time setup in `docs/SETUP.md`. No secrets in the
  repo except the signing key, which is committed on purpose at the owner's request: never replace it
  (Android only installs updates signed with the same key).
- The updater installs only from this repo's `pickless-*` releases over HTTPS and only if the SHA-256
  matches the manifest.
- All personal data stays on the phone. Logs sent with the suggestion box are diagnostics only unless
  the owner ticks "include my data".
- Pickless gives suspects and estimates, not diagnoses; keep the red-flag advice (blood, black stool,
  weight loss, night symptoms, fever: see a doctor).

## Versioning
`VERSION` holds MAJOR.MINOR; CI appends its run number, which is also the `versionCode`. Never rename
the release workflow (its run number would restart and updates would stop installing). The Room
database has its own schema version: bump it and add a migration when tables change.
