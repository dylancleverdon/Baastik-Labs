import java.util.Properties
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.serialization)
    alias(libs.plugins.ksp)
}

// VERSION holds MAJOR.MINOR; CI appends its run number (PICKLESS_BUILD_NUMBER), which is also the
// versionCode, so every published build installs over the previous one.
val versionBase = rootProject.file("VERSION").readText().trim()
val buildNumber = (System.getenv("PICKLESS_BUILD_NUMBER") ?: "0").toInt()
val signing = Properties().apply { rootProject.file("app/signing/signing.properties").inputStream().use { load(it) } }
val repoSlug = "dylancleverdon/Baastik-Labs"

android {
    namespace = "com.baastiklabs.pickless"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.baastiklabs.pickless"
        minSdk = 26
        targetSdk = 36
        versionCode = buildNumber.coerceAtLeast(1)
        versionName = "$versionBase.$buildNumber"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        // The owner's phone is 64-bit ARM; bundling ML Kit's models for every ABI would triple the APK.
        ndk { abiFilters += listOf("arm64-v8a") }
        buildConfigField("String", "REPO", "\"$repoSlug\"")
        buildConfigField("String", "UPDATE_MANIFEST_URL", "\"https://github.com/$repoSlug/releases/download/pickless-channel/manifest.json\"")
        buildConfigField("String", "DATA_MANIFEST_URL", "\"https://github.com/$repoSlug/releases/download/pickless-data/packs.json\"")
    }

    signingConfigs {
        create("release") {
            storeFile = rootProject.file("app/signing/" + signing.getProperty("storeFile"))
            storePassword = signing.getProperty("storePassword")
            keyAlias = signing.getProperty("keyAlias")
            keyPassword = signing.getProperty("keyPassword")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("release")
        }
        debug {
            signingConfig = signingConfigs.getByName("release")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    packaging {
        resources { excludes += setOf("/META-INF/{AL2.0,LGPL2.1}", "/META-INF/LICENSE*", "/META-INF/NOTICE*") }
    }

    testOptions {
        unitTests {
            isIncludeAndroidResources = true
            all {
                it.maxHeapSize = "3g"
                it.systemProperty("robolectric.dependency.repo.url", "https://maven-central.storage-download.googleapis.com/maven2")
                it.systemProperty("pickless.screenshots", rootProject.layout.buildDirectory.dir("screenshots").get().asFile.path)
                // Show the failing line in the CI log, not only in the uploaded report.
                it.testLogging { events("failed"); exceptionFormat = org.gradle.api.tasks.testing.logging.TestExceptionFormat.FULL }
            }
        }
    }

    lint {
        abortOnError = true
        checkReleaseBuilds = true
        disable += setOf("MissingTranslation", "ObsoleteLintCustomCheck", "GradleDependency", "AndroidGradlePluginVersion", "NewerVersionAvailable", "OldTargetApi")
    }
}

kotlin {
    compilerOptions { jvmTarget.set(JvmTarget.JVM_17) }
}

ksp {
    arg("room.schemaLocation", "$projectDir/schemas")
    arg("room.generateKotlin", "true")
}

dependencies {
    implementation(project(":core"))
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.navigation.compose)
    implementation(platform(libs.compose.bom))
    implementation(libs.compose.ui)
    implementation(libs.compose.ui.graphics)
    implementation(libs.compose.ui.tooling.preview)
    implementation(libs.compose.material3)
    implementation(libs.compose.material.icons)
    debugImplementation(libs.compose.ui.tooling)
    implementation(libs.room.runtime)
    implementation(libs.room.ktx)
    ksp(libs.room.compiler)
    implementation(libs.work.runtime)
    implementation(libs.datastore.preferences)
    implementation(libs.camera.core)
    implementation(libs.camera.camera2)
    implementation(libs.camera.lifecycle)
    implementation(libs.camera.view)
    implementation(libs.guava)
    implementation(libs.mlkit.barcode)
    implementation(libs.mlkit.text)
    implementation(libs.glance.appwidget)
    implementation(libs.glance.material3)
    implementation(libs.biometric)
    implementation(libs.health.connect)
    implementation(libs.kotlinx.serialization.json)
    implementation(libs.kotlinx.coroutines.android)
    implementation(libs.okhttp)

    testImplementation(libs.junit)
    testImplementation(libs.robolectric)
    testImplementation(libs.androidx.test.core)
    testImplementation(libs.androidx.test.ext.junit)
    testImplementation(libs.kotlinx.coroutines.test)
    testImplementation(libs.okhttp.mockwebserver)
    testImplementation(platform(libs.compose.bom))
    testImplementation(libs.compose.ui.test.junit4)
    debugImplementation(libs.compose.ui.test.manifest)
}
