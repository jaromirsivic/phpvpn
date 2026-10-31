// Gradle root of the Android client (plan-android-v001, Kotlin part).
// `bridge` wraps libphpvpn-android.so and the IPC v1 client, `app` is the
// Compose application and the VpnService.
pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("com\\.google.*")
                includeGroupByRegex("androidx.*")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "phpvpn-android"
include(":bridge")
include(":app")
