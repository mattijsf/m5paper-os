plugins {
    id("com.android.application")
}

android {
    namespace = "nl.paperos.companion"
    compileSdk = 36

    defaultConfig {
        applicationId = "nl.paperos.companion"
        minSdk = 33
        targetSdk = 36
        versionCode = 1
        versionName = "0.1"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
