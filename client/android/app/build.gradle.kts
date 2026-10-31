// `app`: the Compose application, the VpnService, the notification and the
// boot receiver (plan-android-v001, Kotlin part). WP00 ships a placeholder
// activity that shows the native version.
plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.serialization)
}

// One source of truth for the version: project(... VERSION x.y.z) in
// client/CMakeLists.txt. versionCode = major * 10000 + minor * 100 + patch.
val clientVersion: String = run {
    val cmake = rootProject.file("../CMakeLists.txt").readText()
    Regex("""project\(phpvpn-client\s+VERSION\s+(\d+\.\d+\.\d+)""").find(cmake)?.groupValues?.get(1)
        ?: error("client/CMakeLists.txt has no project VERSION")
}
val clientVersionCode: Int = clientVersion.split('.').map { it.toInt() }
    .let { (major, minor, patch) -> major * 10000 + minor * 100 + patch }

android {
    // The Kotlin namespace is separate from the applicationId on purpose:
    // the identifier is provisional (R2) and changing it must not move sources.
    namespace = "phpvpn.android"
    // compileSdk 37 is required by the current androidx releases; the runtime
    // behaviour is still that of targetSdk 36 (plan-android-v001).
    compileSdk = 37
    compileSdkMinor = 2

    defaultConfig {
        applicationId = "io.github.jaromirsivic.phpvpn"
        minSdk = 34
        targetSdk = 36
        versionCode = clientVersionCode
        versionName = clientVersion
        // Installs a test application with a scripted IPC transport (WP08 UI tests).
        testInstrumentationRunner = "phpvpn.android.PhpvpnTestRunner"
        ndk {
            // R4: 64-bit only. Also drops the 32-bit native libraries that
            // androidx dependencies would otherwise package.
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    buildTypes {
        release {
            // Release signing, R8 and the symbol stripping policy are WP12.
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // IPC reply builders shared by the JVM tests and the Compose UI tests.
    sourceSets {
        getByName("test").kotlin.directories.add("src/testShared/kotlin")
        getByName("androidTest").kotlin.directories.add("src/testShared/kotlin")
    }

    buildFeatures {
        compose = true
        // About (34) shows the build's own version and type (WP08).
        buildConfig = true
    }
}

dependencies {
    implementation(project(":bridge"))
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.navigation.compose)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.lifecycle.service)
    implementation(libs.kotlinx.coroutines.android)
    implementation(libs.kotlinx.serialization.json)

    debugImplementation(libs.androidx.compose.ui.tooling)
    debugImplementation(libs.androidx.compose.ui.test.manifest)

    testImplementation(libs.junit)
    testImplementation(libs.kotlinx.coroutines.test)
    androidTestImplementation(libs.androidx.test.ext.junit)
    androidTestImplementation(libs.androidx.test.runner)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.compose.ui.test.junit4)
}
