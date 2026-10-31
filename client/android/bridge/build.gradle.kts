// `bridge`: the Android library around libphpvpn-android.so. It owns the
// native build (client/CMakeLists.txt with PHPVPN_PLATFORM=android), the JNI
// declarations and, from WP06 on, the IPC v1 client and its JSON models.
import java.util.Properties

plugins {
    alias(libs.plugins.android.library)
}

val localProperties = Properties().apply {
    val file = rootProject.file("local.properties")
    if (file.isFile) file.inputStream().use { load(it) }
}

// Root of the prebuilt OpenSSL and curl (client/tools/android/build-deps.sh,
// WP01): <root>/<abi>/{include,lib}. Empty until WP01, the native build then
// skips the crypto provider and the curl transport.
val androidDeps: String = localProperties.getProperty("phpvpn.androidDeps")
    ?: System.getenv("PHPVPN_ANDROID_DEPS")
    ?: ""

android {
    namespace = "phpvpn.android.bridge"
    compileSdk = 37
    compileSdkMinor = 2
    ndkVersion = "28.2.13676358"

    defaultConfig {
        minSdk = 34
        consumerProguardFiles("consumer-rules.pro")
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DPHPVPN_BUILD_TRAY=OFF",
                    "-DBUILD_TESTING=OFF",
                    "-DANDROID_STL=c++_static",
                    "-DPHPVPN_ANDROID_DEPS=$androidDeps",
                )
                targets += "phpvpn-android"
            }
        }
        ndk {
            // R4: 64-bit only, arm64 devices and the x86_64 emulator.
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.31.6"
        }
    }

    buildTypes {
        debug {
            // T-N03 (plan-android-v001 WP02): libphpvpn-android-tests.so runs
            // the POSIX layer tests inside the application sandbox for the
            // instrumented test. Release builds never configure the target.
            externalNativeBuild {
                cmake {
                    arguments += "-DPHPVPN_ANDROID_NATIVE_TESTS=ON"
                    targets += "phpvpn-android-tests"
                }
            }
        }
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    implementation(libs.kotlinx.coroutines.android)
    implementation(libs.kotlinx.serialization.json)
    testImplementation(libs.junit)
    testImplementation(libs.kotlinx.coroutines.test)
    androidTestImplementation(libs.androidx.test.ext.junit)
    androidTestImplementation(libs.androidx.test.runner)
}
