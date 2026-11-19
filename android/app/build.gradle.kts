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
        versionCode = 3
        versionName = "1.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    testOptions {
        unitTests.all {
            // Shared protocol vectors (docs/COMPANION.md section 9).
            it.systemProperty("vectors", rootProject.file("../tests/fixtures/companion/vectors.json").absolutePath)
            // Remote control against a running simulator (RemoteTest.againstTheSimulator): -Premote.sim=localhost:8266
            (project.findProperty("remote.sim") as String?)?.let { sim -> it.systemProperty("remote.sim", sim) }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    // QR scanning: CameraX for frames, ZXing core (pure Java) to decode. No Google Play services.
    implementation("androidx.activity:activity:1.13.0")
    implementation("androidx.camera:camera-camera2:1.6.2")
    implementation("androidx.camera:camera-lifecycle:1.6.2")
    implementation("androidx.camera:camera-view:1.6.2")
    implementation("com.google.zxing:core:3.5.4")
    testImplementation("junit:junit:4.13.2")
    // The real org.json for host tests (android.jar only has stubs).
    testImplementation("org.json:json:20250517")
    androidTestImplementation("androidx.test:runner:1.7.0")
    androidTestImplementation("androidx.test.ext:junit:1.3.0")
}
