# SingScoring SDK

Cross-platform SDK for real-time singing evaluation. A user sings along to a
reference track; the SDK compares their mic input against a reference MIDI
melody and returns an integer score in **[10, 99]** (pass ≥ 60).

- **Android:** `singscoring.aar` (arm64-v8a, armeabi-v7a, x86_64) + Kotlin API.
- **iOS:** `SingScoring.xcframework` (device + simulator) + Obj-C/Swift API.
- **Demo APK:** plays the backing track via ExoPlayer, captures mic, shows score.

No network needed — songs are self-contained zip bundles.

Current version: **0.2.0**. See [CHANGELOG.md](CHANGELOG.md) for what shipped.
ABI reference: [docs/ABI.md](docs/ABI.md).

---

## Quickstart

### Android

```kotlin
// 1. Point at a .zip on disk (cache dir, downloaded file, asset-extract, etc.)
val session = SingScoringSession.open(zipPath)

// 2. From an AudioRecord(ENCODING_PCM_FLOAT) callback:
session.feedPcm(floatBuffer, sampleRate = 44100, count = framesRead)

// 3. When the user taps "stop":
val score = session.finalizeScore()   // 10..99
session.close()
```

See [`demo-android/`](demo-android/) for an end-to-end example.

### iOS (Swift)

```swift
guard let session = SingScoringSession(zipPath: zipURL.path) else {
    fatalError("failed to open song zip")
}

// From an AVAudioEngine input tap:
buffer.floatChannelData?[0].withMemoryRebound(to: Float.self, capacity: n) { ptr in
    session.feedPCM(ptr, count: n, sampleRate: 44100)
}

let score = session.finalizeScore()
```

Build: `./scripts/build-ios-xcframework.sh` (macOS only). Full guide:
[`bindings/ios/README.md`](bindings/ios/README.md).

---

## Song zip layout

Each song zip contains four files named after the song code:

| File | Role |
|---|---|
| `[code]_chorus.mp3` | Backing track played while the user sings. |
| `[code]_chorus.mid` | Reference monophonic vocal melody. Sole scoring input. |
| `[code]_chorus.lrc` | Lyrics — **display only**, never scored. |
| `[code]_chorus.json` | `{songCode, name, singer, rhythm, duration}`. `duration` is the MP3 length, not the scoring horizon. |

The MIDI always ends before the MP3 — the MP3 has instrumental intro/outro.
Scoring horizon is the last MIDI note's end time, not the MP3 length.

---

## Scoring design

- **Per-note, not per-frame.** The scorer walks every reference MIDI note and
  takes the median detected pitch over that note's time window. Silent
  instrumental gaps are never penalized.
- **Pitch detection:** YIN on 40ms windows, 10ms hop. Search range clamped
  to 80–800 Hz (covers MIDI 40–84; observed range is 48–75).
- **Per-note score curve:** semitone error → `[0.1, 1.0]`. 0.5-semitone
  tolerance band (vibrato / pitch bend), linear decay to a 3-semitone floor.
- **Aggregate:** duration-weighted mean across notes, mapped `[0, 1] → [10, 99]`.
  Long held notes test real pitch control, so they weigh proportionally more.

The 10 floor means even an unanswered session returns a valid integer.

---

## Build

Shell is MSYS bash on Windows. None of the toolchain is on system PATH by
default — use `scripts/env.sh` to prepend the Android SDK / NDK / JDK:

```bash
. scripts/env.sh
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"

./gradlew :singscoring:assembleDebug        # AAR → bindings/android/build/outputs/aar/
./gradlew :demo-android:assembleDebug       # Demo APK
./gradlew clean
```

Desktop core + tests (Linux / macOS, requires cmake + C++17 toolchain; not
buildable natively on Windows yet):

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

GitHub Actions runs desktop tests + the Android AAR on every push (see
[`.github/workflows/ci.yml`](.github/workflows/ci.yml)).

---

## Repo layout

```
/core                    # Portable C++17 scoring engine (no platform deps)
  /include/singscoring.h # Public C ABI — the sole platform/core boundary
  /src/*.cpp             # MIDI / LRC / JSON / MP3 / zip / YIN / scorer / session
/bindings
  /android               # JNI + Kotlin SingScoringSession
  /ios                   # Obj-C++ shim + xcframework target
/demo-android            # Sample app consuming the AAR (live mic scoring)
/third_party             # Vendored deps: miniz, minimp3
/tests                   # GoogleTest suite — runs in CI
/SongHighlightSamples    # Four fixture zips used for tests + demo content
/scripts                 # env.sh (toolchain paths), build-ios-xcframework.sh
```

---

## Toolchain (pinned)

- JDK 21 (Android Studio bundled at `C:\Program Files\Android\Android Studio\jbr`)
- Android NDK **27.3.13750724**
- CMake 3.22+ (Linux CI / iOS) / 3.31.6 (Android Studio local)
- Gradle 8.9 / AGP 8.6.0 / Kotlin 2.0.21
- minSdk 24 / targetSdk 34 / compileSdk 34
- iOS 13.0+ deployment target

---

## Fixture data caveats

The four bundled songs in `SongHighlightSamples/` are both test fixtures and
demo content. One of them (`7104926136466570.zip` — 离不开你) ships with a
broken MIDI tempo header, so live scoring against it will be wrong until
the data is regenerated. The integration test flags and skips the affected
assertion for that song. The other three are fine.
