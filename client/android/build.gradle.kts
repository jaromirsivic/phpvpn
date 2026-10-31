// Kotlin is compiled by the built-in support of the Android Gradle plugin 9;
// only the Compose compiler and the kotlinx.serialization plugins are applied
// per module.
plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.android.library) apply false
    alias(libs.plugins.kotlin.compose) apply false
    alias(libs.plugins.kotlin.serialization) apply false
}
