# Android integration guide

Drop the SingScoring SDK into another Android app. You hand it a song zip
and mic PCM, it hands you back an integer score.

Companion references:
- [ABI.md](ABI.md) — the C surface the Android binding wraps.
- [demo-android/](../demo-android) — a full reference app using the SDK.

## 1. What you get

- `SingScoringSession` in Kotlin package `com.sensen.singscoring`.
- A single `libsingscoring.so` per ABI, bundled in the AAR. No extra runtime
  deps — the C++ stdlib is statically linked (`-DANDROID_STL=c++_static`).
- Score range: `[10, 99]`. Pass ≥ 60.

## 2. Requirements

| | |
| --- | --- |
| minSdk | 24 |
| ABIs | arm64-v8a, armeabi-v7a, x86_64 |
| Kotlin | 2.0.21 or compatible |
| JDK to compile consumers | 17 |
| `RECORD_AUDIO` permission | yes (for mic capture; request at runtime) |

## 3. Getting the AAR

Pick one:

**Build from source**

```bash
export JAVA_HOME=/path/to/jdk-17   # or Android Studio's bundled JBR
./gradlew :singscoring:assembleRelease
# output: bindings/android/build/outputs/aar/singscoring-release.aar
```

**Download from CI** — every push to `main` produces an AAR as a workflow
artifact. See the `android-build` job in `.github/workflows/ci.yml`.

**Local Maven publish** (optional) — not wired up yet. If you want this,
add the `maven-publish` plugin to `bindings/android/build.gradle.kts`.

## 4. Gradle wiring

Drop the AAR into `app/libs/` and:

```kotlin
// app/build.gradle.kts
android {
    defaultConfig {
        minSdk = 24
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64") }
    }
}

dependencies {
    implementation(files("libs/singscoring-release.aar"))
}
```

If you use Maven later, replace the `files(...)` line with
`implementation("com.sensen:singscoring:<version>")`.

## 5. Permissions

```xml
<!-- AndroidManifest.xml -->
<uses-permission android:name="android.permission.RECORD_AUDIO" />
```

Request at runtime before capturing — see `MainActivity.kt` in the demo
for the `ActivityResultContracts.RequestPermission` pattern.

## 6. API reference

Full Kotlin source: `bindings/android/src/main/kotlin/com/sensen/singscoring/SingScoringSession.kt`.

### One-shot (recommended)

```kotlin
val score: Int = SingScoringSession.score(
    zipPath = "/data/.../song.zip",   // absolute path
    samples = floats,                 // mono float32 PCM, sample 0 = MIDI t=0
    sampleRate = 44100,               // whatever AudioRecord gave you
)
// score ∈ [10, 99]; 60 is the pass line
```

This is the path you want 95% of the time. Record the chorus into one
`FloatArray` then score it in a single call.

### Streaming (only if you really can't buffer)

```kotlin
SingScoringSession.open(zipPath).use { session ->
    while (recording) {
        val n = audioRecord.read(buf, 0, buf.size, AudioRecord.READ_BLOCKING)
        session.feedPcm(buf, sampleRate = 44100, count = n)
    }
    val score = session.finalizeScore()
}
```

`use { }` calls `close()` — don't leak the native handle.

### Utilities

```kotlin
SingScoringSession.version           // e.g. "0.3.0"
SingScoringSession.melodyEndMs(zip)  // scoring horizon in ms, -1 on failure
```

Use `melodyEndMs` to auto-stop capture at the end of the reference melody.
**Do not** use `json.duration` (MP3 length) or the LRC last-line time —
see CLAUDE.md "Scoring design invariants" for why.

## 7. Audio format

Non-negotiable:

- **Mono.** Downmix stereo yourself before calling `feedPcm` / `score`.
- **32-bit float PCM**, range `[-1.0, 1.0]`.
- **Sample rate**: pass whatever `AudioRecord` negotiated. The engine
  handles rates from ~16 kHz upward; the demo uses 44100.
- **Sample 0 = MIDI t=0.** The SDK does not align, trim leading silence,
  or accept an offset. You start the mic in sync with the lyrics scroll.
  The demo does a 3-second countdown splash before starting capture.

Reference recorder: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/AudioRecorder.kt`
— `CHANNEL_IN_MONO` + `ENCODING_PCM_FLOAT` at 44100 Hz.

## 8. Song zip layout

The SDK expects a zip with (at minimum):

- A MIDI file — single-track monophonic vocal melody in notes 48–75
  (~130–620 Hz).
- A `json` descriptor the engine reads for metadata.

Sample zips live in `SongHighlightSamples/` at repo root and are what the
demo + tests use as fixtures. You pass the **zip file path**, not an
extracted directory — the engine reads it directly.

## 9. End-to-end recipe

```kotlin
class RecordAndScore(private val context: Context) {
    private val pcm = ArrayList<FloatArray>()
    private var recorder: AudioRecorder? = null
    private val sampleRate = 44100

    fun start() {
        recorder = AudioRecorder(sampleRate) { samples, count ->
            pcm.add(samples.copyOf(count))
        }.also { it.start() }
    }

    fun stopAndScore(zipPath: String): Int {
        recorder?.stop(); recorder = null
        val flat = FloatArray(pcm.sumOf { it.size })
        var off = 0
        for (chunk in pcm) { chunk.copyInto(flat, off); off += chunk.size }
        pcm.clear()
        return SingScoringSession.score(zipPath, flat, sampleRate)
    }
}
```

Run `stopAndScore` on a background thread — the engine's one-shot call
does disk I/O and FFTs.

## 10. Versioning

`SingScoringSession.version` returns the SDK version string. The Kotlin
API surface is stable across patch releases; the C ABI behind it is the
single source of truth — see [ABI.md](ABI.md). Bumping the SDK is a
matter of dropping in a newer AAR.

## Gotchas

- **No backing track during recording.** If the user hears the reference
  MP3 through the device speaker, mic bleed inflates the score (the
  playback is tonally identical to the MIDI by construction). The demo
  omits playback for this reason.
- **The `.zip` must be on local disk.** The engine opens it with stdio;
  you can't pass a `content://` URI or stream it straight from the
  network. Download to `cacheDir` first (see `SongStaging.kt`).
- **One session, one song.** Don't feed samples from song A into a
  session opened for song B. Use the one-shot API and this can't happen.
- **Known broken sample**: `7104926136466570.zip` declares 74 BPM but is
  actually ~120 BPM. Scoring against it will be wrong; skip if you hit it.
