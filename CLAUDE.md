# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

Cross-platform SDK for real-time singing evaluation. Android (AAR) + iOS (xcframework) bindings wrap a portable C++17 scoring engine that compares a user's microphone input to a reference MIDI melody and returns an integer score in **[10, 99]** (pass ≥ 60).

Status: **Phase 0** (scaffolding) is complete. The C++ core currently ships **stub scoring** — `ss_finalize_score` returns 10 regardless of input. Real DSP (MIDI parser, YIN pitch detector, per-note scorer) lands in Phase 1. See [readme.md](readme.md) for the phase plan.

## Architecture

The scoring logic is **intentionally one-way**:

```
Kotlin SingScoringSession (bindings/android/src/main/kotlin/...)
  ↓ JNI
jni.cpp  (bindings/android/src/main/cpp/jni.cpp)
  ↓ C ABI — the only contract between platform and core
singscoring.h  (core/include/singscoring.h)
  ↓
core/src/*.cpp  — portable C++17, no platform deps
```

Both bindings (Android JNI and, eventually, iOS Obj-C++) talk to the core **only through the five functions in `singscoring.h`**: `ss_open / ss_feed_pcm / ss_finalize_score / ss_close / ss_version`. Never leak platform types (JNIEnv, NSString, file handles) into `core/`; never leak C++ types (`std::string`, templates) across the ABI boundary.

The Android AAR module lives at `bindings/android/` but is registered in Gradle as `:singscoring` (see `settings.gradle.kts`). Its `CMakeLists.txt` uses `add_subdirectory` to pull in `core/` — there is no second copy of the scoring code.

### Scoring design invariants (relevant in Phase 1+)

Decisions already baked into the plan from inspecting `SongHighlightSamples/*.zip`:

- **Score iterates over MIDI notes, not wall-clock time.** Silent instrumental gaps can be 40%+ of the chorus; a naive frame-by-frame comparator penalizes correct silence.
- **`json.duration` is the MP3 length, not the scoring horizon.** Use `last_midi_note.end_ms` for scoring bounds.
- **LRC is display-only.** Never feed LRC timestamps into the scorer; they can be misaligned from the MIDI intentionally (e.g., duet lines).
- **All observed MIDIs are single-track monophonic vocal melodies** in MIDI 48–75 (~130–620 Hz). Pitch detector search range clamped accordingly.
- Aggregate per-note scores weighted by `note.duration_ms` (long held notes test real pitch control).

## Common commands

Shell is MSYS bash on Windows. None of the toolchain is on system PATH — use the env script first if you need `cmake`/`adb`/`java` in the shell:

```bash
. scripts/env.sh                        # exports ANDROID_HOME, ANDROID_NDK_HOME, JAVA_HOME, prepends PATH
```

Gradle invocations do **not** need `env.sh` (wrapper finds the JDK via `org.gradle.java.home` auto-detection + `local.properties`), but they do need `JAVA_HOME` set in the calling shell on first run:

```bash
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"

./gradlew :singscoring:assembleDebug    # build the AAR → bindings/android/build/outputs/aar/
./gradlew :demo-android:assembleDebug   # build demo APK
./gradlew clean                         # wipe all build outputs
./gradlew :singscoring:externalNativeBuildDebug   # just the native CMake step
```

Desktop (non-Android) core + GoogleTest — **not buildable on Windows yet** (no local C++ compiler). Skip until CI picks this up, or run from a Linux/macOS shell with `cmake` and a C++17 toolchain:

```bash
cmake -S . -B build-desktop
cmake --build build-desktop
ctest --test-dir build-desktop --output-on-failure
ctest --test-dir build-desktop -R Session.open_null_path_returns_null --output-on-failure  # single test
```

## Toolchain versions (pinned)

- JDK 21 (bundled with Android Studio at `C:\Program Files\Android\Android Studio\jbr`)
- Android SDK platforms 34/35/36.1, build-tools 34.0.0/36.1.0/37.0.0
- NDK **27.3.13750724** (pinned in `bindings/android/build.gradle.kts` via `ndkVersion`)
- CMake **3.31.6** (pinned in `local.properties` via `cmake.dir` and in AGP config)
- Gradle 8.9 / AGP 8.6.0 / Kotlin 2.0.21 (see `gradle/libs.versions.toml`)
- minSdk 24 / targetSdk 34 / compileSdk 34
- Package namespace: `com.sensen.singscoring` (SDK), `com.sensen.singscoring.demo` (demo app)

## Conventions worth knowing

- **Commits never attribute Claude** — no `Co-Authored-By:` trailer, no "Generated with Claude Code" footer. Plain subject + body.
- Samples in `SongHighlightSamples/*.zip` are both demo content (bundled into the APK via `sourceSets["main"].assets.srcDirs("../SongHighlightSamples")` in `demo-android/build.gradle.kts`) and scoring fixtures (Phase 1 calibration targets).
- Only LF-normalization warnings are expected on `git add` (the project uses LF in source, Windows checks out CRLF — harmless).
- The NDK's own `android.toolchain.cmake` emits `cmake_minimum_required < 3.10` deprecation warnings on CMake 3.31. These are from Google's files, not ours — ignore until a newer NDK ships.
