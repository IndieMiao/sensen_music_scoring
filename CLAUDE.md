# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

Cross-platform SDK for real-time singing evaluation. Android (AAR) + iOS (xcframework) bindings wrap a portable C++17 scoring engine that compares a user's microphone input to a reference MIDI melody and returns an integer score in **[10, 99]** (pass ≥ 60).

Status as of **0.3.0**: full pipeline shipping (MIDI parser + YIN pitch detector + per-note scorer + `[10, 99]` aggregate) on Android (live AAR + demo APK) and iOS (scaffolded Obj-C++ framework + xcframework build script). See [CHANGELOG.md](CHANGELOG.md) for per-release notes and [docs/ABI.md](docs/ABI.md) for the stability contract.


# Tool Permissions
- **Always Allowed:** "Bash(git *)","Bash(gh *)","Bash(docker *)","Bash(npm *)","Bash(python *)","Bash","Edit","Read","Write","Grep","Glob","Bash(adb *)","Bash(tail *)","Bash(python3 *)"

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

Both bindings (Android JNI and iOS Obj-C++) talk to the core **only through the seven functions in `singscoring.h`**: `ss_score` (one-shot, the standard path) plus the streaming quartet `ss_open / ss_feed_pcm / ss_finalize_score / ss_close`, `ss_version`, and `ss_melody_end_ms` (scoring horizon — last MIDI note end, for UIs that auto-stop capture). Never leak platform types (JNIEnv, NSString, file handles) into `core/`; never leak C++ types (`std::string`, templates) across the ABI boundary.

The Android AAR module lives at `bindings/android/` but is registered in Gradle as `:singscoring` (see `settings.gradle.kts`). Its `CMakeLists.txt` uses `add_subdirectory` to pull in `core/` — there is no second copy of the scoring code.

### Scoring design invariants

Decisions baked into the engine from inspecting `SongHighlightSamples/*.zip`:

- **Score iterates over MIDI notes, not wall-clock time.** Silent instrumental gaps can be 40%+ of the chorus; a naive frame-by-frame comparator penalizes correct silence.
- **`json.duration` is the MP3 length, not the scoring horizon.** Use `last_midi_note.end_ms` for scoring bounds.
- **LRC is display-only.** Never feed LRC timestamps into the scorer; they can be misaligned from the MIDI intentionally (e.g., duet lines).
- **All observed MIDIs are single-track monophonic vocal melodies** in MIDI 48–75 (~130–620 Hz). Pitch detector search range clamped accordingly.
- Aggregate per-note scores weighted by `note.duration_ms` (long held notes test real pitch control).
- **One-shot is the standard contract.** The app records the chorus into a single PCM buffer and calls `ss_score(...)`; sample 0 is treated as MIDI t=0. The SDK does not align, trim leading silence, or take an offset parameter — the caller starts capture in sync with the lyrics scroll.
- **No backing track in the demo.** The user sings to the scrolling lyrics. This avoids speaker-to-mic leakage that would otherwise make YIN detect the playback's pitch (which matches the MIDI by construction) and inflate scores during instrumental gaps.

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

Desktop (non-Android) core + GoogleTest — **not buildable on Windows yet** (no local C++ compiler). Runs automatically in CI on every push; to run by hand, use a Linux/macOS shell with `cmake` and a C++17 toolchain:

```bash
cmake -S . -B build-desktop
cmake --build build-desktop
ctest --test-dir build-desktop --output-on-failure
ctest --test-dir build-desktop -R Session.open_null_path_returns_null --output-on-failure  # single test
```

## CI

`.github/workflows/ci.yml` runs two jobs on every push/PR to main:

- `desktop-tests` — Ubuntu, CMake + Ninja, runs `ctest`. This is the only place the core C++ tests actually execute today.
- `android-build` — Ubuntu + JDK 21 + NDK 27.3 via sdkmanager, builds the AAR, uploads it as a workflow artifact.

If a test or build fails locally but CI passes (or vice versa), treat CI as authoritative for the core — it has a real C++ toolchain.

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
- Samples in `SongHighlightSamples/*.zip` are both demo content (bundled into the APK via `sourceSets["main"].assets.srcDirs("../SongHighlightSamples")` in `demo-android/build.gradle.kts`) and scoring fixtures exercised by `tests/test_song_integration.cpp` and `tests/test_session_scoring.cpp`.
- `7104926136466570.zip` has a broken MIDI tempo (74 BPM declared, ~120 BPM actual). The integration test allowlists this one song with a relaxed expectation; scoring against live input for it is known wrong.
- Version bumps: edit `core/include/singscoring_version.h` + `bindings/ios/Info.plist.in` + `bindings/ios/CMakeLists.txt` (the `MACOSX_FRAMEWORK_SHORT_VERSION_STRING` line) + a CHANGELOG entry. Nothing else reads the version at runtime.
- Only LF-normalization warnings are expected on `git add` (the project uses LF in source, Windows checks out CRLF — harmless).
- The NDK's own `android.toolchain.cmake` emits `cmake_minimum_required < 3.10` deprecation warnings on CMake 3.31. These are from Google's files, not ours — ignore until a newer NDK ships.

