// Maven Central is reached through Google's public mirror of it first: the plain Maven Central
// host rate-limits shared build machines (HTTP 429), and Gradle doesn't fall through on a 429.
val mavenCentralMirror = "https://maven-central.storage-download.googleapis.com/maven2/"

pluginManagement {
    repositories {
        google()
        maven("https://maven-central.storage-download.googleapis.com/maven2/")
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        maven(mavenCentralMirror)
        mavenCentral()
    }
}

rootProject.name = "Pickless"
include(":core", ":app")
