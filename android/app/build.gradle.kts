// The test app: the engine's headless replay runner and sim-math tests, run on the phone.
//
// The native programs are not built here: the CMake presets build them
// (cmake --build --preset android-arm64-release), and stageFafNative packages them as
// lib<name>.so, the only files Android extracts into nativeLibraryDir, from where an app may
// start a program. tests/sim_math/golden.txt goes into the assets.

import com.android.build.gradle.internal.api.BaseVariantOutputImpl

plugins {
    id("com.android.application")
}

val repoRoot: File = rootDir.parentFile
val fafAbis: List<String> =
    providers.gradleProperty("fafAbis").get().split(",").map { it.trim() }.filter { it.isNotEmpty() }

fun git(vararg args: String): String =
    providers.exec {
        commandLine("git", *args)
        workingDir = repoRoot
    }.standardOutput.asText.get().trim()

// The NDK the engine is built with (cmake/toolchains/android-pins.cmake): AGP strips the
// programs with its llvm-strip.
val pinnedNdk: String =
    Regex("""FAF_ANDROID_PINNED_NDK_REVISION\s+"([^"]+)"""")
        .find(repoRoot.resolve("cmake/toolchains/android-pins.cmake").readText())!!.groupValues[1]

android {
    namespace = "io.github.m3rt1n99.fafre"
    compileSdk = 35
    ndkVersion = pinnedNdk

    defaultConfig {
        // The id of the earlier launcher releases: an update keeps the game data they imported.
        applicationId = "io.github.m3rt1n99.fafre"
        minSdk = 26
        targetSdk = 35
        // Only grows on a branch, so every build of a newer commit installs over an older one.
        versionCode = git("rev-list", "--count", "HEAD").toInt()
        versionName = providers.gradleProperty("fafVersionName").get()
        buildConfigField("String", "GIT_COMMIT", "\"${git("rev-parse", "--short=10", "HEAD")}\"")
        ndk {
            abiFilters += fafAbis
        }
    }

    buildFeatures {
        buildConfig = true
    }

    // Android installs an update only with the installed app's key. FAF_ANDROID_KEYSTORE names a
    // key of its own; otherwise the standard debug key (~/.android/debug.keystore), which signed
    // the earlier releases.
    signingConfigs {
        create("fafre") {
            val keystore = System.getenv("FAF_ANDROID_KEYSTORE")
            storeFile = file(keystore ?: "${System.getProperty("user.home")}/.android/debug.keystore")
            storePassword = System.getenv("FAF_ANDROID_KEYSTORE_PASSWORD") ?: "android"
            keyAlias = System.getenv("FAF_ANDROID_KEY_ALIAS") ?: "androiddebugkey"
            keyPassword = System.getenv("FAF_ANDROID_KEY_PASSWORD") ?: "android"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("fafre")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // Extracted on install (extractNativeLibs): the app starts libfafheadless.so as a process.
    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }

    sourceSets["main"].jniLibs.srcDir(layout.buildDirectory.dir("fafNative"))
    sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("fafAssets"))
}

// faf-re-android-<version>-<abis>.apk
android.applicationVariants.all {
    val variant = this
    outputs.all {
        (this as BaseVariantOutputImpl).outputFileName =
            "faf-re-android-${variant.versionName}-${fafAbis.joinToString("-")}.apk"
    }
}

dependencies {
    // FAF's vault serves replays as zstd frames; java.util.zip has no zstd. 1.5.7-13 and later
    // require compileSdk 37.
    implementation("com.github.luben:zstd-jni:1.5.7-12@aar")
}

val stageFafNative by tasks.registering(Sync::class) {
    description = "Packages the engine's Android programs (CMake preset builds) as lib<name>.so."
    into(layout.buildDirectory.dir("fafNative"))
    for (abi in fafAbis) {
        val dir = repoRoot.resolve(providers.gradleProperty("fafNativeDir.$abi").get())
        for ((program, library) in listOf("headless" to "libfafheadless.so", "faf_sim_math_tests" to "libfafsimmath.so")) {
            from(dir.resolve(program)) {
                into(abi)
                rename { library }
            }
        }
    }
    doFirst {
        for (abi in fafAbis) {
            val dir = repoRoot.resolve(providers.gradleProperty("fafNativeDir.$abi").get())
            for (program in listOf("headless", "faf_sim_math_tests")) {
                if (!dir.resolve(program).isFile) {
                    throw GradleException(
                        "$dir/$program is missing: build the engine first " +
                            "(cmake --preset android-<abi>-release, cmake --build --preset android-<abi>-release)")
                }
            }
        }
    }
}

val stageFafAssets by tasks.registering(Sync::class) {
    description = "Puts the sim-math golden values into the assets."
    from(repoRoot.resolve("tests/sim_math/golden.txt"))
    into(layout.buildDirectory.dir("fafAssets"))
}

tasks.named("preBuild") {
    dependsOn(stageFafNative, stageFafAssets)
}
