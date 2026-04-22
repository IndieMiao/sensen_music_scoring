# One-shot PCM scoring + scrolling-lyrics demo (v0.3.0) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an additive `ss_score(...)` one-shot C-ABI entry, wire it through Android (Kotlin) and iOS (Obj-C++), then rework the Android demo to record the full chorus once and score in one call while showing scrolling lyrics with no backing track. Bump to 0.3.0.

**Architecture:** `ss_score` is a thin wrapper over the existing `ss_open + ss_feed_pcm + ss_finalize_score + ss_close` sequence — no new scoring logic. The streaming entry points stay for advanced callers. The demo's new state machine is `PICKER → COUNTDOWN → RECORDING → SCORING → RESULT`; LRC is parsed in Kotlin (SDK boundary stays clean). Spec: [`docs/superpowers/specs/2026-04-22-oneshot-pcm-scoring-design.md`](../specs/2026-04-22-oneshot-pcm-scoring-design.md).

**Tech Stack:** C++17 (core), GoogleTest (CI on Ubuntu), JNI + Kotlin 2.0.21 / AGP 8.6.0 (Android), Obj-C++ + CMake framework target (iOS).

---

## Important: where C++ tests run

Desktop core + GoogleTest are **not buildable on the Windows dev machine** (no local C++ toolchain). The TDD cycle for `core/` and `tests/` happens in CI:

- `git push` triggers `.github/workflows/ci.yml` which runs `ctest` on Ubuntu — this is the authoritative test verifier.
- Local sanity is gained from `./gradlew :singscoring:assembleDebug`, which compiles `core/src/*.cpp` through the NDK and catches header/syntax errors quickly.

For each core-side task, the plan's "verify failing" / "verify passing" steps are run via CI (push and read the workflow status). Do not skip the failing-test step — push the failing test alone first, confirm CI shows red, then push the implementation.

If you have access to a Linux/macOS shell with a C++17 toolchain, you can run locally instead:

```bash
cmake -S . -B build-desktop
cmake --build build-desktop
ctest --test-dir build-desktop --output-on-failure
```

---

## File map

**Created:**
- `tests/test_session_oneshot.cpp` — GoogleTest cases for `ss_score`.
- `demo-android/src/main/kotlin/com/sensen/singscoring/demo/LrcParser.kt` — LRC `[mm:ss.xx] text` → `List<LrcLine>`.
- `demo-android/src/main/kotlin/com/sensen/singscoring/demo/LyricsScrollView.kt` — scrolling lyrics view driven by an elapsed-ms supplier.

**Modified:**
- `core/include/singscoring.h` — add `ss_score` declaration; add "advanced" doc note on streaming functions.
- `core/include/singscoring_version.h` — bump to 0.3.0.
- `core/src/session.cpp` — implement `ss_score`.
- `tests/CMakeLists.txt` — add `test_session_oneshot.cpp` to the test target.
- `bindings/android/src/main/cpp/jni.cpp` — add `nativeScore`.
- `bindings/android/src/main/kotlin/com/sensen/singscoring/SingScoringSession.kt` — add static `score(...)` and `external nativeScore`.
- `bindings/ios/include/SingScoring/SSCSession.h` — add `+scoreWithZipPath:samples:count:sampleRate:`.
- `bindings/ios/SSCSession.mm` — implement the new class method.
- `bindings/ios/CMakeLists.txt` — bump `MACOSX_FRAMEWORK_SHORT_VERSION_STRING` to 0.3.0.
- `bindings/ios/Info.plist.in` — bump `CFBundleShortVersionString` to 0.3.0.
- `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt` — full rewrite for the 5-state flow.
- `demo-android/src/main/kotlin/com/sensen/singscoring/demo/AudioRecorder.kt` — no signature change; usage changes to accumulate.
- `demo-android/build.gradle.kts` — drop `media3-exoplayer` and `media3-ui` deps.
- `gradle/libs.versions.toml` — drop `media3` version + `media3-exoplayer` / `media3-ui` library entries.
- `docs/ABI.md` — document `ss_score`; mark streaming variant as advanced.
- `CHANGELOG.md` — `## 0.3.0` entry.
- `CLAUDE.md` — update architecture and invariants for the new contract.

---

## Task 1: Add `ss_score` to the C ABI (TDD)

**Files:**
- Create: `tests/test_session_oneshot.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `core/include/singscoring.h`
- Modify: `core/src/session.cpp`

- [ ] **Step 1.1: Write the failing test**

Create `tests/test_session_oneshot.cpp`:

```cpp
// One-shot ss_score: equivalence with the open/feed/finalize/close sequence
// plus the standard null/empty argument floor returns.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "fixtures.h"
#include "mp3_decoder.h"
#include "singscoring.h"
#include "song.h"

TEST(SessionOneShot, null_zip_returns_floor) {
    EXPECT_EQ(ss_score(nullptr, nullptr, 0, 44100), 10);
}

TEST(SessionOneShot, valid_zip_null_pcm_returns_floor) {
    const std::string zip = ss::fixture_path("7162848696587380.zip");
    EXPECT_EQ(ss_score(zip.c_str(), nullptr, 0, 44100), 10);
}

TEST(SessionOneShot, valid_zip_zero_samples_returns_floor) {
    const std::string zip = ss::fixture_path("7162848696587380.zip");
    float dummy = 0.0f;
    EXPECT_EQ(ss_score(zip.c_str(), &dummy, 0, 44100), 10);
}

TEST(SessionOneShot, equivalent_to_streaming_path) {
    // Same input must produce the same score whether driven via the streaming
    // entry points or the new one-shot. This pins the wrapper as a true
    // equivalence rather than a re-implementation.
    const std::string zip = ss::fixture_path("7162848696587380.zip");

    auto song = ss::load_song(zip.c_str());
    ASSERT_NE(song, nullptr);
    auto pcm = ss::decode_mp3(song->mp3_data.data(), song->mp3_data.size());
    ASSERT_FALSE(pcm.samples.empty());

    // Streaming path: chunk it as a real audio thread would.
    int streaming;
    {
        ss_session* s = ss_open(zip.c_str());
        ASSERT_NE(s, nullptr);
        const int chunk = 4096;
        for (size_t off = 0; off < pcm.samples.size(); off += chunk) {
            int n = int(std::min(size_t(chunk), pcm.samples.size() - off));
            ss_feed_pcm(s, pcm.samples.data() + off, n, pcm.sample_rate);
        }
        streaming = ss_finalize_score(s);
        ss_close(s);
    }

    // One-shot path: hand the whole buffer over.
    int oneshot = ss_score(
        zip.c_str(), pcm.samples.data(), int(pcm.samples.size()), pcm.sample_rate);

    EXPECT_EQ(streaming, oneshot)
        << "streaming=" << streaming << " oneshot=" << oneshot;
    EXPECT_GE(oneshot, 10);
    EXPECT_LE(oneshot, 99);
}
```

- [ ] **Step 1.2: Wire the test into the build**

Edit `tests/CMakeLists.txt` — add `test_session_oneshot.cpp` to the `add_executable(singscoring_tests ...)` source list, alphabetically near `test_session_scoring.cpp`:

```cmake
add_executable(singscoring_tests
    test_version.cpp
    test_session.cpp
    test_session_oneshot.cpp
    test_midi_parser.cpp
    test_lrc_parser.cpp
    test_json_parser.cpp
    test_zip_loader.cpp
    test_song_integration.cpp
    test_mp3_decoder.cpp
    test_pitch_detector.cpp
    test_scorer.cpp
    test_session_scoring.cpp
)
```

- [ ] **Step 1.3: Push and verify the test fails in CI**

```bash
git add tests/test_session_oneshot.cpp tests/CMakeLists.txt
git commit -m "test: add failing ss_score one-shot tests"
git push
```

Open the GitHub Actions run for this push. Expected:
- `desktop-tests` job FAILS at the compile step with an error like "use of undeclared identifier 'ss_score'".
- `android-build` job FAILS for the same reason (NDK compile of `core/src/session.cpp` is referenced from the test indirectly, but core itself doesn't yet declare/implement it — the AAR build links against `core` only, so it actually still passes). If `android-build` passes, that's fine; the desktop failure is what matters.

- [ ] **Step 1.4: Add the declaration to the header**

Edit `core/include/singscoring.h` — append the new declaration before the closing `extern "C"` block, after `ss_version`:

```c
/**
 * One-shot scoring: open the song zip, score the supplied PCM buffer
 * against the chorus MIDI, and release everything. Equivalent to
 * ss_open + ss_feed_pcm + ss_finalize_score + ss_close, but doesn't
 * require the caller to manage a session handle.
 *
 * PCM contract: mono float32, sample_rate Hz. Sample 0 is treated as
 * MIDI t=0 — caller is responsible for starting capture in sync with
 * the chorus reference (e.g., when the lyrics scroll begins).
 *
 * Returns a score in [10, 99]. Returns 10 on any failure
 * (unreadable zip, empty PCM, parse failure, etc.).
 */
int ss_score(const char* zip_path,
             const float* samples, int n_samples, int sample_rate);
```

- [ ] **Step 1.5: Implement in session.cpp**

Edit `core/src/session.cpp` — append below `ss_close`:

```cpp
extern "C" int ss_score(const char* zip_path,
                        const float* samples, int n_samples, int sample_rate) {
    ss_session* s = ss_open(zip_path);
    if (!s) return 10;
    ss_feed_pcm(s, samples, n_samples, sample_rate);
    int score = ss_finalize_score(s);
    ss_close(s);
    return score;
}
```

- [ ] **Step 1.6: Local sanity — AAR build compiles core**

```bash
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
./gradlew :singscoring:assembleDebug
```

Expected: BUILD SUCCESSFUL. This proves the header + impl compile under the NDK toolchain without needing CI.

- [ ] **Step 1.7: Push and verify tests pass in CI**

```bash
git add core/include/singscoring.h core/src/session.cpp
git commit -m "feat: add ss_score one-shot C ABI entry"
git push
```

Expected: both CI jobs (`desktop-tests`, `android-build`) PASS. The new `SessionOneShot.*` tests appear in the ctest output as passing.

---

## Task 2: Wire `ss_score` through Android (JNI + Kotlin)

**Files:**
- Modify: `bindings/android/src/main/cpp/jni.cpp`
- Modify: `bindings/android/src/main/kotlin/com/sensen/singscoring/SingScoringSession.kt`

- [ ] **Step 2.1: Add the JNI native function**

Edit `bindings/android/src/main/cpp/jni.cpp` — add this block before the closing `extern "C"`:

```cpp
JNIEXPORT jint JNICALL
Java_com_sensen_singscoring_SingScoringSession_nativeScore(
        JNIEnv* env, jclass, jstring zipPath,
        jfloatArray samples, jint count, jint sampleRate) {
    if (!zipPath) return 10;
    const char* path = env->GetStringUTFChars(zipPath, nullptr);
    int score = 10;
    if (samples && count > 0) {
        jsize len = env->GetArrayLength(samples);
        if (count > len) count = len;
        jfloat* data = env->GetFloatArrayElements(samples, nullptr);
        score = ss_score(path, data, static_cast<int>(count), static_cast<int>(sampleRate));
        env->ReleaseFloatArrayElements(samples, data, JNI_ABORT);
    } else {
        // Empty PCM still goes through ss_score so error semantics stay identical
        // (open succeeds → feed no-ops → finalize returns 10).
        score = ss_score(path, nullptr, 0, static_cast<int>(sampleRate));
    }
    env->ReleaseStringUTFChars(zipPath, path);
    return score;
}
```

- [ ] **Step 2.2: Add the Kotlin static entry**

Edit `bindings/android/src/main/kotlin/com/sensen/singscoring/SingScoringSession.kt` — inside `companion object`, after `nativeVersion()`'s declaration, add:

```kotlin
/**
 * One-shot scoring. Open the song zip, score [samples] (mono float32 at
 * [sampleRate] Hz) against the chorus MIDI, and release the session in a
 * single call. Returns a score in [10, 99]. The first sample is treated
 * as MIDI t=0 — caller starts capture in sync with the lyrics scroll.
 */
@JvmStatic
fun score(
    zipPath: String,
    samples: FloatArray,
    sampleRate: Int,
    count: Int = samples.size
): Int {
    require(count in 0..samples.size) { "count=$count out of range [0, ${samples.size}]" }
    if (count == 0) return 10
    return nativeScore(zipPath, samples, count, sampleRate)
}

@JvmStatic private external fun nativeScore(
    zipPath: String, samples: FloatArray, count: Int, sampleRate: Int
): Int
```

- [ ] **Step 2.3: Build the AAR locally**

```bash
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
./gradlew :singscoring:assembleDebug
```

Expected: BUILD SUCCESSFUL. (No instrumented tests for the JNI surface today — coverage of `ss_score` itself comes from Task 1's CI tests.)

- [ ] **Step 2.4: Commit**

```bash
git add bindings/android/src/main/cpp/jni.cpp \
        bindings/android/src/main/kotlin/com/sensen/singscoring/SingScoringSession.kt
git commit -m "feat(android): expose ss_score via Kotlin SingScoringSession.score"
```

---

## Task 3: Wire `ss_score` through iOS (Obj-C++)

**Files:**
- Modify: `bindings/ios/include/SingScoring/SSCSession.h`
- Modify: `bindings/ios/SSCSession.mm`

- [ ] **Step 3.1: Add the class-method declaration**

Edit `bindings/ios/include/SingScoring/SSCSession.h` — inside `@interface`, after the `feedPCM:...` instance method, add:

```objc
/// One-shot scoring. Open the zip, score the provided mono float32 PCM
/// (`samples` of length `count`) against the chorus MIDI, and release
/// the session. Returns a score in [10, 99]. The first sample is treated
/// as MIDI t=0 — caller aligns capture to the lyrics scroll.
+ (NSInteger)scoreWithZipPath:(NSString *)zipPath
                      samples:(const float *)samples
                        count:(NSInteger)count
                   sampleRate:(NSInteger)sampleRate
    NS_SWIFT_NAME(score(zipPath:samples:count:sampleRate:));
```

- [ ] **Step 3.2: Implement**

Edit `bindings/ios/SSCSession.mm` — add inside `@implementation SSCSession`, after `finalizeScore`:

```objc
+ (NSInteger)scoreWithZipPath:(NSString *)zipPath
                      samples:(const float *)samples
                        count:(NSInteger)count
                   sampleRate:(NSInteger)sampleRate
{
    if (!zipPath) return 10;
    return ss_score(zipPath.UTF8String,
                    samples,
                    (int)count,
                    (int)sampleRate);
}
```

- [ ] **Step 3.3: Commit**

(No iOS build available on this Windows machine; macOS CI / a separate iOS build run validates the Obj-C++ side. The header contract is consistent with the C ABI added in Task 1.)

```bash
git add bindings/ios/include/SingScoring/SSCSession.h \
        bindings/ios/SSCSession.mm
git commit -m "feat(ios): expose ss_score via SSCSession.score class method"
```

---

## Task 4: LRC parser in the demo

**Files:**
- Create: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/LrcParser.kt`

(No demo-android unit-test target exists today; matches existing project convention. The parser is small and exercised by the demo at runtime.)

- [ ] **Step 4.1: Write the parser**

Create `demo-android/src/main/kotlin/com/sensen/singscoring/demo/LrcParser.kt`:

```kotlin
package com.sensen.singscoring.demo

/**
 * Tiny LRC parser for the scrolling-lyrics view. Mirrors the C++ core's
 * lrc_parser at the level we need for display:
 *
 *   - `[mm:ss.xx] text`  → one entry per timestamp
 *   - `[mm:ss.xxx] text` → milliseconds also accepted
 *   - `[ti:...]`, `[ar:...]`, etc. → skipped
 *   - one line may carry multiple leading timestamps (each emits an entry)
 *   - returned list is sorted by `timeMs`
 *
 * Display-only: the SDK never sees these timestamps. This parser stays
 * with the demo, not the SDK.
 */
data class LrcLine(val timeMs: Long, val text: String)

object LrcParser {
    private val tagRegex = Regex("""\[(\d+):(\d+)(?:\.(\d+))?]""")

    fun parse(input: String): List<LrcLine> {
        val out = mutableListOf<LrcLine>()
        for (rawLine in input.lineSequence()) {
            val line = rawLine.trimEnd('\r')
            if (line.isEmpty()) continue

            val matches = tagRegex.findAll(line).toList()
            if (matches.isEmpty()) continue  // metadata lines like [ti:...] don't match (no colon-numeric)

            val text = line.substring(matches.last().range.last + 1).trim()
            if (text.isEmpty()) continue

            for (m in matches) {
                val mm = m.groupValues[1].toLong()
                val ss = m.groupValues[2].toLong()
                val frac = m.groupValues[3]
                val fracMs = when (frac.length) {
                    0 -> 0L
                    1 -> frac.toLong() * 100
                    2 -> frac.toLong() * 10
                    3 -> frac.toLong()
                    else -> frac.substring(0, 3).toLong()
                }
                out.add(LrcLine(mm * 60_000L + ss * 1_000L + fracMs, text))
            }
        }
        out.sortBy { it.timeMs }
        return out
    }
}
```

- [ ] **Step 4.2: Verify it compiles via the AAR/demo build**

```bash
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
./gradlew :demo-android:compileDebugKotlin
```

Expected: BUILD SUCCESSFUL. (Parser isn't called yet — Task 6 wires it in.)

- [ ] **Step 4.3: Commit**

```bash
git add demo-android/src/main/kotlin/com/sensen/singscoring/demo/LrcParser.kt
git commit -m "feat(demo): add LrcParser for scrolling-lyrics view"
```

---

## Task 5: Scrolling lyrics view

**Files:**
- Create: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/LyricsScrollView.kt`

- [ ] **Step 5.1: Write the view**

Create `demo-android/src/main/kotlin/com/sensen/singscoring/demo/LyricsScrollView.kt`:

```kotlin
package com.sensen.singscoring.demo

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.view.Choreographer
import android.view.View

/**
 * Centered scrolling lyrics. Driven by an external elapsed-time supplier so
 * the view doesn't own the clock — the activity's recording start time is
 * the truth, and this view just renders against it.
 *
 *   - active line is centered horizontally and vertically, drawn larger.
 *   - upcoming lines fade above; past lines fade below (or vice versa,
 *     depending on scroll direction; we scroll past lines upward).
 *   - smooth interpolation between adjacent lines using a fractional
 *     position so motion is continuous.
 */
class LyricsScrollView(context: Context) : View(context) {

    private var lines: List<LrcLine> = emptyList()
    private var elapsedMs: () -> Long = { 0L }

    private val activePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 56f
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }
    private val inactivePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#80FFFFFF")  // 50% white
        textSize = 40f
        textAlign = Paint.Align.CENTER
    }

    private val frameCallback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            invalidate()
            if (isAttachedToWindow) Choreographer.getInstance().postFrameCallback(this)
        }
    }

    fun setLines(lines: List<LrcLine>) { this.lines = lines; invalidate() }
    fun setClock(supplier: () -> Long) { this.elapsedMs = supplier }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        setBackgroundColor(Color.parseColor("#101010"))
        Choreographer.getInstance().postFrameCallback(frameCallback)
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        Choreographer.getInstance().removeFrameCallback(frameCallback)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (lines.isEmpty()) return

        val now = elapsedMs()

        // Find the active line: largest index whose timeMs <= now.
        var active = -1
        for (i in lines.indices) {
            if (lines[i].timeMs <= now) active = i else break
        }

        // Fractional position for smooth scroll between lines.
        val frac: Float = if (active >= 0 && active + 1 < lines.size) {
            val span = (lines[active + 1].timeMs - lines[active].timeMs).coerceAtLeast(1)
            ((now - lines[active].timeMs).toFloat() / span).coerceIn(0f, 1f)
        } else 0f

        val centerX = width / 2f
        val centerY = height / 2f
        val rowH = 80f

        // Anchor: y of the active line (slides up as `frac` grows toward the next line).
        val activeY = centerY - frac * rowH

        for (i in lines.indices) {
            val y = activeY + (i - active) * rowH
            if (y < -rowH || y > height + rowH) continue
            val paint = if (i == active) activePaint else inactivePaint
            canvas.drawText(lines[i].text, centerX, y, paint)
        }
    }
}
```

- [ ] **Step 5.2: Verify it compiles**

```bash
./gradlew :demo-android:compileDebugKotlin
```

Expected: BUILD SUCCESSFUL.

- [ ] **Step 5.3: Commit**

```bash
git add demo-android/src/main/kotlin/com/sensen/singscoring/demo/LyricsScrollView.kt
git commit -m "feat(demo): add LyricsScrollView driven by external clock"
```

---

## Task 6: Rewrite MainActivity for the 5-state flow + drop ExoPlayer

**Files:**
- Modify: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`
- Modify: `demo-android/build.gradle.kts`
- Modify: `gradle/libs.versions.toml`

- [ ] **Step 6.1: Drop the media3 dependency**

Edit `demo-android/build.gradle.kts` — remove these two lines from the `dependencies { ... }` block:

```kotlin
    implementation(libs.media3.exoplayer)
    implementation(libs.media3.ui)
```

Edit `gradle/libs.versions.toml`:

- Remove the `media3 = "1.4.1"` line under `[versions]`.
- Remove the `media3-exoplayer` and `media3-ui` lines under `[libraries]`.

- [ ] **Step 6.2: Rewrite MainActivity**

Replace `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt` with:

```kotlin
package com.sensen.singscoring.demo

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Gravity
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.sensen.singscoring.SingScoringSession
import java.io.File
import java.util.zip.ZipInputStream
import kotlin.concurrent.thread

class MainActivity : AppCompatActivity() {

    private enum class State { PICKER, COUNTDOWN, RECORDING, SCORING, RESULT }

    private val sampleRate = 44100
    private var state = State.PICKER
    private var recorder: AudioRecorder? = null
    private var pendingSong: SongAssets.Song? = null
    private var stagedZipPath: String? = null
    private var lyrics: List<LrcLine> = emptyList()

    // Recording buffer (owned by the activity; the recorder just appends).
    private val pcm = ArrayList<FloatArray>()
    private var pcmTotalSamples = 0
    private var recordingStartMs = 0L  // SystemClock.elapsedRealtime when "Sing!" hits

    private val main = Handler(Looper.getMainLooper())
    private var autoStopRunnable: Runnable? = null

    private val root by lazy { FrameLayout(this) }

    private val requestMicPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) pendingSong?.let { startCountdown(it) }
        else toastLike("Microphone permission denied")
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        root.setPadding(48, 96, 48, 48)
        setContentView(root)
        renderPicker()
    }

    override fun onDestroy() {
        super.onDestroy()
        cancelAutoStop()
        recorder?.stop(); recorder = null
    }

    // --- screens -----------------------------------------------------------

    private fun renderPicker() {
        state = State.PICKER
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView("SingScoring demo"))
        col.addView(subtitleView("SDK ${SingScoringSession.version} — pick a song"))

        val scroll = ScrollView(this)
        val list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        SongAssets.list(this).forEach { song ->
            list.addView(Button(this).apply {
                text = "${song.displayName}  (${song.code})"
                setOnClickListener { onSongPicked(song) }
                layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                    .apply { topMargin = 16 }
            })
        }
        scroll.addView(list)
        col.addView(scroll, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
        root.addView(col)
    }

    private fun renderCountdown(song: SongAssets.Song, secondsLeft: Int) {
        state = State.COUNTDOWN
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView("🎤  ${song.displayName}"))
        col.addView(subtitleView("Get ready to sing the chorus."))
        col.addView(TextView(this).apply {
            text = if (secondsLeft > 0) secondsLeft.toString() else "Sing!"
            textSize = 96f
            gravity = Gravity.CENTER
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 96 }
        })
        root.addView(col)
    }

    private fun renderRecording(song: SongAssets.Song) {
        state = State.RECORDING
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView("🎤  ${song.displayName}"))

        val view = LyricsScrollView(this).apply {
            setLines(lyrics)
            setClock { SystemClock.elapsedRealtime() - recordingStartMs }
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f)
                .apply { topMargin = 16; bottomMargin = 16 }
        }
        col.addView(view)

        col.addView(Button(this).apply {
            text = "Stop & score"
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
            setOnClickListener { finishAndScore() }
        })
        root.addView(col)
    }

    private fun renderScoring(song: SongAssets.Song) {
        state = State.SCORING
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView(song.displayName))
        col.addView(subtitleView("Scoring…"))
        root.addView(col)
    }

    private fun renderResult(song: SongAssets.Song, score: Int) {
        state = State.RESULT
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView(song.displayName))

        val passed = score >= 60
        col.addView(TextView(this).apply {
            text = score.toString()
            textSize = 96f
            setTextColor(if (passed) Color.parseColor("#2E7D32") else Color.parseColor("#C62828"))
            gravity = Gravity.CENTER
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 64; bottomMargin = 16 }
        })
        col.addView(subtitleView(if (passed) "Passed" else "Needs work (pass ≥ 60)"))

        col.addView(Button(this).apply {
            text = "Pick another song"
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 64 }
            setOnClickListener { renderPicker() }
        })
        root.addView(col)
    }

    // --- flow --------------------------------------------------------------

    private fun onSongPicked(song: SongAssets.Song) {
        pendingSong = song
        val granted = ContextCompat.checkSelfPermission(
            this, Manifest.permission.RECORD_AUDIO
        ) == PackageManager.PERMISSION_GRANTED
        if (granted) startCountdown(song) else requestMicPermission.launch(Manifest.permission.RECORD_AUDIO)
    }

    private fun startCountdown(song: SongAssets.Song) {
        // Stage the zip and parse LRC up-front so the recording phase has nothing to wait on.
        val staged = try {
            SongAssets.stage(this, song)
        } catch (e: Exception) {
            toastLike("Failed to stage song: ${e.message}")
            return
        }
        stagedZipPath = staged.zipPath
        lyrics = readLyrics(staged.zipPath, song.code)

        // 3 → 2 → 1 → "Sing!" → start recording.
        renderCountdown(song, 3)
        main.postDelayed({ renderCountdown(song, 2) }, 1000)
        main.postDelayed({ renderCountdown(song, 1) }, 2000)
        main.postDelayed({
            renderCountdown(song, 0)            // "Sing!"
            beginRecording(song)                // also picks up the t=0 instant
        }, 3000)
    }

    private fun beginRecording(song: SongAssets.Song) {
        pcm.clear()
        pcmTotalSamples = 0
        recordingStartMs = SystemClock.elapsedRealtime()

        val rec = AudioRecorder(sampleRate) { samples, count ->
            // Copy into our own array so the recorder can reuse its buffer.
            val copy = FloatArray(count)
            System.arraycopy(samples, 0, copy, 0, count)
            synchronized(pcm) {
                pcm.add(copy)
                pcmTotalSamples += count
            }
        }
        try {
            rec.start()
        } catch (e: Exception) {
            toastLike("Recorder start failed: ${e.message}")
            return
        }
        recorder = rec

        // Move from "Sing!" splash to the actual scrolling-lyrics screen after a brief beat
        // so the user sees the splash render. 250 ms is enough for the eye.
        main.postDelayed({ if (state == State.COUNTDOWN) renderRecording(song) }, 250)

        // Auto-stop a short tail past the last lyric line (or at 30 s if LRC is empty).
        val tailMs = (lyrics.lastOrNull()?.timeMs ?: 30_000L) + 1500L
        autoStopRunnable = Runnable { if (state == State.RECORDING) finishAndScore() }
        main.postDelayed(autoStopRunnable!!, tailMs)
    }

    private fun finishAndScore() {
        val song = pendingSong ?: return
        val zip = stagedZipPath ?: return
        cancelAutoStop()
        recorder?.stop(); recorder = null

        renderScoring(song)

        val flat: FloatArray = synchronized(pcm) {
            val out = FloatArray(pcmTotalSamples)
            var off = 0
            for (chunk in pcm) {
                System.arraycopy(chunk, 0, out, off, chunk.size)
                off += chunk.size
            }
            out
        }

        thread(name = "ss-scoring", isDaemon = true) {
            val score = try {
                SingScoringSession.score(zip, flat, sampleRate)
            } catch (_: Exception) { 10 }
            main.post { renderResult(song, score) }
        }
    }

    private fun cancelAutoStop() {
        autoStopRunnable?.let { main.removeCallbacks(it) }
        autoStopRunnable = null
    }

    // --- helpers -----------------------------------------------------------

    private fun readLyrics(zipPath: String, songCode: String): List<LrcLine> {
        val target = "${songCode}_chorus.lrc"
        return try {
            ZipInputStream(File(zipPath).inputStream()).use { zis ->
                while (true) {
                    val e = zis.nextEntry ?: return emptyList()
                    if (e.name.endsWith(target)) {
                        val text = zis.readBytes().toString(Charsets.UTF_8)
                        return LrcParser.parse(text)
                    }
                }
                @Suppress("UNREACHABLE_CODE") emptyList()
            }
        } catch (_: Exception) { emptyList() }
    }

    // --- view helpers ------------------------------------------------------

    private fun titleView(text: String) = TextView(this).apply {
        this.text = text
        textSize = 26f
        layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
    }

    private fun subtitleView(text: String) = TextView(this).apply {
        this.text = text
        textSize = 16f
        layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
            .apply { topMargin = 8; bottomMargin = 16 }
    }

    private fun toastLike(msg: String) {
        android.util.Log.w("SingScoringDemo", msg)
        TextView(this).apply {
            text = msg
            setTextColor(Color.parseColor("#C62828"))
        }.also { root.addView(it) }
    }
}
```

- [ ] **Step 6.3: Verify the demo builds**

```bash
./gradlew :demo-android:assembleDebug
```

Expected: BUILD SUCCESSFUL. The output APK lives under `demo-android/build/outputs/apk/debug/`.

- [ ] **Step 6.4: Manual smoke (optional, when a device is available)**

Install + run; confirm: pick song → countdown 3-2-1-Sing → scrolling lyrics with no audio playing → tap Stop → score appears. Stop button returns a score even on a near-empty recording (≥ 10).

- [ ] **Step 6.5: Commit**

```bash
git add demo-android/build.gradle.kts \
        gradle/libs.versions.toml \
        demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt
git commit -m "feat(demo): rewrite for one-shot scoring + scrolling lyrics, drop ExoPlayer"
```

---

## Task 7: Version bump + documentation updates

**Files:**
- Modify: `core/include/singscoring_version.h`
- Modify: `bindings/ios/Info.plist.in`
- Modify: `bindings/ios/CMakeLists.txt`
- Modify: `core/include/singscoring.h`
- Modify: `docs/ABI.md`
- Modify: `CHANGELOG.md`
- Modify: `CLAUDE.md`

- [ ] **Step 7.1: Bump version constants**

Edit `core/include/singscoring_version.h`:

```c
#define SSC_VERSION_MAJOR 0
#define SSC_VERSION_MINOR 3
#define SSC_VERSION_PATCH 0
```

Edit `bindings/ios/Info.plist.in` — change:

```xml
<key>CFBundleShortVersionString</key>
<string>0.3.0</string>
```

Edit `bindings/ios/CMakeLists.txt` — change:

```cmake
MACOSX_FRAMEWORK_SHORT_VERSION_STRING 0.3.0
```

- [ ] **Step 7.2: Add the "advanced" note to the streaming functions**

Edit `core/include/singscoring.h` — replace the existing doc-comment for `ss_open` with a one-line preamble plus the existing text. Concretely, add this sentence at the top of `ss_open`'s doc-comment:

```c
/**
 * Advanced — for chunked / pre-warmed flows. Most callers should use ss_score
 * (below) for the standard one-shot path.
 *
 * Open a scoring session from a song zip on disk.
 * The zip must contain [songCode]_chorus.{mp3,mid,lrc,json}.
 * Returns NULL on failure.
 */
ss_session* ss_open(const char* zip_path);
```

- [ ] **Step 7.3: Update `docs/ABI.md`**

Replace the C-snippet block with:

```c
ss_session* ss_open(const char* zip_path);
void        ss_feed_pcm(ss_session*, const float* samples, int n, int sample_rate);
int         ss_finalize_score(ss_session*);   // 10..99
void        ss_close(ss_session*);
const char* ss_version(void);

// One-shot — recommended path for app integrations.
int         ss_score(const char* zip_path,
                     const float* samples, int n, int sample_rate);  // 10..99
```

Add a new subsection above "Lifecycle":

```markdown
## One-shot scoring

`ss_score` opens the zip, scores the supplied PCM buffer, and releases
everything in one call. Sample 0 is treated as MIDI t=0 — the caller is
responsible for starting capture in sync with the chorus reference (e.g.,
when a lyrics scroll begins). Returns the same `[10, 99]` integer as
`ss_finalize_score`. This is the recommended entry point for app
integrations; the `open / feed_pcm / finalize_score / close` quartet is
kept for advanced flows that need a session handle (chunked upload,
pre-warming, parallel takes against one open session).
```

- [ ] **Step 7.4: Add CHANGELOG entry**

Edit `CHANGELOG.md` — insert above the `## 0.2.0` section:

```markdown
## 0.3.0 — 2026-04-22

### Added
- `ss_score(zip_path, samples, n, sample_rate)` — one-shot C-ABI entry
  that opens, feeds, finalizes, and closes in a single call. Surfaced in
  Kotlin as `SingScoringSession.score(...)` (static) and in Obj-C as
  `+[SSCSession scoreWithZipPath:samples:count:sampleRate:]`.

### Changed
- The Android demo no longer plays a backing track. Flow is now
  `pick song → countdown → scrolling lyrics + record → score`. Lyrics
  scroll from the parsed LRC; the user sings to them with no audio
  reference. ExoPlayer / `media3` dependency removed from the demo.
- Scoring contract is documented as one-shot first: `PCM[0]` is treated
  as `MIDI t=0`. The streaming `ss_open / ss_feed_pcm / ss_finalize_score
  / ss_close` quartet remains available for advanced flows.

```

- [ ] **Step 7.5: Update CLAUDE.md**

Edit `CLAUDE.md`:

In the "What this project is" section, change `Status as of **0.2.0**` to `Status as of **0.3.0**`.

In the "Architecture" section, change the line listing the five functions to mention six and the new contract:

```markdown
Both bindings (Android JNI and iOS Obj-C++) talk to the core **only through the six functions in `singscoring.h`**: `ss_score` (one-shot, the standard path) plus the streaming quartet `ss_open / ss_feed_pcm / ss_finalize_score / ss_close` and `ss_version`. Never leak platform types (JNIEnv, NSString, file handles) into `core/`; never leak C++ types (`std::string`, templates) across the ABI boundary.
```

In the "Scoring design invariants" section, append:

```markdown
- **One-shot is the standard contract.** The app records the chorus into a single PCM buffer and calls `ss_score(...)`; sample 0 is treated as MIDI t=0. The SDK does not align, trim leading silence, or take an offset parameter — the caller starts capture in sync with the lyrics scroll.
- **No backing track in the demo.** The user sings to the scrolling lyrics. This avoids speaker-to-mic leakage that would otherwise make YIN detect the playback's pitch (which matches the MIDI by construction) and inflate scores during instrumental gaps.
```

In the "Conventions worth knowing" section, update the "Version bumps" line to:

```markdown
- Version bumps: edit `core/include/singscoring_version.h` + `bindings/ios/Info.plist.in` + `bindings/ios/CMakeLists.txt` (the `MACOSX_FRAMEWORK_SHORT_VERSION_STRING` line) + a CHANGELOG entry. Nothing else reads the version at runtime.
```

(unchanged from today — left as-is here just to confirm no edit is needed in this paragraph.)

- [ ] **Step 7.6: Verify both builds still compile**

```bash
./gradlew :singscoring:assembleDebug :demo-android:assembleDebug
```

Expected: BUILD SUCCESSFUL.

- [ ] **Step 7.7: Push and verify CI is green**

```bash
git add core/include/singscoring_version.h \
        core/include/singscoring.h \
        bindings/ios/Info.plist.in \
        bindings/ios/CMakeLists.txt \
        docs/ABI.md CHANGELOG.md CLAUDE.md
git commit -m "docs: bump to 0.3.0; document ss_score and one-shot contract"
git push
```

Expected: `desktop-tests` and `android-build` both PASS. The version test (`tests/test_version.cpp`) — if it asserts the string — will pin the new `0.3.0` value; if a test fails because of an old version literal, update that test in this commit.

---

## Self-review

- **Spec coverage:** D1 PCM coverage (Tasks 6 + spec doc); D2 ignore offset (Task 6's countdown is the alignment mechanism, no SDK code); D3 additive `ss_score` (Tasks 1–3); D4 LRC ownership in app (Task 4 + Task 6 `readLyrics`); D5 no backing track (Task 6 drops media3, no playback). ABI section → Task 1. Bindings → Tasks 2–3. Demo rework → Tasks 4–6. Error handling → already in Task 1's tests + Task 2 JNI guards. Testing → Task 1. Doc updates → Task 7. ✅
- **Placeholders:** none.
- **Type consistency:** `ss_score` signature matches across `singscoring.h` decl (Task 1), `session.cpp` impl (Task 1), JNI `nativeScore` (Task 2), Kotlin `nativeScore` external (Task 2), and Obj-C `+scoreWithZipPath:samples:count:sampleRate:` (Task 3). All return `int` / `jint` / `NSInteger`. Kotlin `score(...)` keeps the `count` overload pattern from existing `feedPcm`. `LrcLine(timeMs: Long, text: String)` used identically by `LrcParser`, `LyricsScrollView.setLines`, and `MainActivity.readLyrics`. ✅
