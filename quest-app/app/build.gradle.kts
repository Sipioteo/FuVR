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

// Cap'n Proto code generation. Runs `capnp compile` against ../proto/fuvr.capnp
// and emits C++ sources into src/main/cpp/proto_gen.
val capnpProtoFile = rootProject.file("../proto/fuvr.capnp")
val capnpOutDir = layout.projectDirectory.dir("src/main/cpp/proto_gen")

val generateCapnp = tasks.register<Exec>("generateCapnp") {
    val outDir = capnpOutDir.asFile
    inputs.file(capnpProtoFile)
    outputs.dir(outDir)
    doFirst { outDir.mkdirs() }
    workingDir = capnpProtoFile.parentFile
    commandLine = listOf(
        "capnp", "compile",
        "-oc++:${outDir.absolutePath}",
        "--src-prefix=${capnpProtoFile.parentFile.absolutePath}",
        capnpProtoFile.absolutePath
    )
    isIgnoreExitValue = true
}

tasks.matching { it.name.startsWith("externalNativeBuild") || it.name.startsWith("configureCMake") }
    .configureEach { dependsOn(generateCapnp) }
