plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
}

android {
    namespace = "com.sensen.singscoring"
    compileSdk = 34
    ndkVersion = "27.3.13750724"

    defaultConfig {
        minSdk = 24
        consumerProguardFiles("consumer-rules.pro")

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++17", "-fvisibility=hidden")
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            // No `version` — AGP picks the best CMake available. Locally that
            // resolves to whatever's in Sdk/cmake/; on CI (no Sdk/cmake/)
            // AGP falls back to the cmake package from sdkmanager.
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    // Ship one optimized native binary per ABI, period. Consumers (including
    // the demo's debug APK) fall back to this variant — see matchingFallbacks
    // in demo-android/build.gradle.kts. Native debug builds (-O0) are never
    // useful for the SDK's hot path; the Kotlin/Java side stays debuggable.
    androidComponents {
        beforeVariants(selector().withBuildType("debug")) { variant ->
            variant.enable = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    sourceSets["main"].java.srcDirs("src/main/kotlin")
}

dependencies {
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.test.ext.junit)
}
