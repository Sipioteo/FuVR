// SPDX-License-Identifier: Apache-2.0

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.fuvr.quest"
    compileSdk = 34
    ndkVersion = "26.3.11579264"

    defaultConfig {
        applicationId = "com.fuvr.quest"
        minSdk = 29
        targetSdk = 33
        versionCode = 1
        versionName = "0.1.0"

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_PLATFORM=android-29"
                )
                cppFlags += listOf("-std=c++20", "-fno-exceptions", "-fno-rtti")
            }
        }
        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    buildTypes {
        debug {
            isDebuggable = true
            isJniDebuggable = true
        }
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildFeatures {
        prefab = true
    }

    packaging {
        jniLibs.useLegacyPackaging = false
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("org.khronos.openxr:openxr_loader_for_android:1.1.36")
}

// Cap'n Proto code generation. Fails clearly if `capnp` isn't on PATH.
val capnpProtoFile = rootProject.file("../proto/fuvr.capnp")
val capnpOutDir = layout.projectDirectory.dir("src/main/cpp/proto_gen")

fun resolveCapnpBinary(): String? {
    val pathEntries = (System.getenv("PATH") ?: "").split(File.pathSeparator)
    for (entry in pathEntries) {
        val candidate = file("$entry/capnp")
        if (candidate.exists() && candidate.canExecute()) return candidate.absolutePath
    }
    return null
}

val generateCapnp = tasks.register<Exec>("generateCapnp") {
    val outDir = capnpOutDir.asFile
    inputs.file(capnpProtoFile)
    outputs.dir(outDir)
    doFirst {
        if (resolveCapnpBinary() == null) {
            throw GradleException(
                "`capnp` (Cap'n Proto compiler) was not found on PATH. " +
                "Install it (e.g. `brew install capnp` on macOS, " +
                "`apt-get install capnproto` on Debian/Ubuntu) and re-run."
            )
        }
        outDir.mkdirs()
    }
    workingDir = capnpProtoFile.parentFile
    commandLine = listOf(
        "capnp", "compile",
        "-oc++:${outDir.absolutePath}",
        "--src-prefix=${capnpProtoFile.parentFile.absolutePath}",
        capnpProtoFile.absolutePath
    )
}

tasks.named("preBuild").configure { dependsOn(generateCapnp) }
tasks.matching { it.name.startsWith("externalNativeBuild") || it.name.startsWith("configureCMake") }
    .configureEach { dependsOn(generateCapnp) }

// Host-side native unit tests for fragment reassembly logic. Compiled with
// the host toolchain (NOT the NDK), so they can run in CI without a device.
val hostTestBuildDir = layout.buildDirectory.dir("host_tests")

val configureHostTests = tasks.register<Exec>("configureHostTests") {
    doFirst { hostTestBuildDir.get().asFile.mkdirs() }
    workingDir = hostTestBuildDir.get().asFile
    commandLine = listOf(
        "cmake",
        "-S", file("src/main/cpp/tests").absolutePath,
        "-B", hostTestBuildDir.get().asFile.absolutePath,
        "-DCMAKE_BUILD_TYPE=Debug"
    )
}

val buildHostTests = tasks.register<Exec>("buildHostTests") {
    dependsOn(configureHostTests)
    workingDir = hostTestBuildDir.get().asFile
    commandLine = listOf("cmake", "--build", hostTestBuildDir.get().asFile.absolutePath)
}

val hostTestFragment = tasks.register<Exec>("hostTestFragment") {
    dependsOn(buildHostTests)
    workingDir = hostTestBuildDir.get().asFile
    commandLine = listOf("./test_fragment_reassembly")
}

val hostTestClockSync = tasks.register<Exec>("hostTestClockSync") {
    dependsOn(buildHostTests)
    workingDir = hostTestBuildDir.get().asFile
    commandLine = listOf("./test_clock_sync")
}

tasks.register("hostTest") {
    dependsOn(hostTestFragment, hostTestClockSync)
}
