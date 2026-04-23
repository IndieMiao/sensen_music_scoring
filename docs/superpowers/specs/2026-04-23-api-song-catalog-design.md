# API-backed song catalog in the Android demo

## Goal

Replace the Android demo's bundled-asset song list with a remote catalog. The demo currently packs all 19 `SongHighlightSamples/*.zip` files into the APK and enumerates them via `ctx.assets.list("")`. After this change, the picker is served by an HTTP API and the chosen song's zip is downloaded to `cacheDir` on first pick.

The C++ integration tests (`tests/test_song_integration.cpp`, `tests/test_session_scoring.cpp`) still use `SongHighlightSamples/*.zip` on disk as fixtures — only the APK packaging changes.

## APIs

Both are the same endpoint with different query params.

**List** (used by the picker):

```
GET http://210.22.95.26:30027/api/audio/querySongInfo?pageNo=1&pageSize=100
```

Only the first page is fetched. The catalog has ~456 songs total; 100 is already more than a demo picker needs.

**Detail** (unused in this change):

```
GET http://210.22.95.26:30027/api/audio/querySongInfo?id=<id>
```

The list response's `resourceUrl` field is a direct HTTPS CDN URL to the song's zip, so no detail call is needed for the download flow. Left unimplemented.

**Response shape (relevant fields only):**

```json
{
  "code": 0,
  "data": {
    "content": [
      { "id": "7104926136527090",
        "name": "Complicated",
        "singer": "Avril Lavigne",
        "rhythm": "fast",
        "resourceUrl": "https://cdn.gensen.sensenjoy.cn/public/grabsing/zip/7104926136527090.zip" }
    ]
  }
}
```

`code != 0` or missing `data.content` is an error. `style`, `packIds`, `duration`, `cover`, `timestamp` are ignored.

## Scope

New / changed files under `demo-android/`:

- **New** `src/main/kotlin/com/sensen/singscoring/demo/SongCatalog.kt` — HTTP + JSON.
- **Rewritten** `src/main/kotlin/com/sensen/singscoring/demo/SongAssets.kt` → `SongStaging.kt` — drops asset enumeration, adds `download(song, ...)`, keeps extraction logic reading from `cacheDir`.
- **Modified** `src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt` — adds `DOWNLOADING` state, picker loading/error substates, two-line buttons.
- **Modified** `src/main/AndroidManifest.xml` — adds `INTERNET` permission and `networkSecurityConfig` reference.
- **New** `src/main/res/xml/network_security_config.xml` — cleartext allowed only for `210.22.95.26`.
- **Modified** `build.gradle.kts` — removes `sourceSets["main"].assets.srcDirs("../SongHighlightSamples")`.

`SongHighlightSamples/*.zip` stay on disk (C++ tests depend on them) but stop being packaged into the APK.

## Data model

```kotlin
data class Song(
    val id: String,        // was "code"
    val name: String,
    val singer: String,
    val rhythm: String,    // "fast" | "slow"
    val zipUrl: String,    // from resourceUrl
)
```

`id` replaces the old `code` everywhere (zip file name `cacheDir/songs/<id>.zip`, mp3 `<id>_chorus.mp3`, LRC lookup `<id>_chorus.lrc`, JSON lookup `<id>_chorus.json`). This matches what the API exposes and matches the inner file naming of every existing sample zip.

## State machine

Additions in **bold**:

```
PICKER (loading | ready | error)
  └─ user taps song
     └─ mic permission check
        └─ **DOWNLOADING** ──(back)──→ PICKER
           └─ zip present in cacheDir
              └─ PREVIEW → COUNTDOWN → RECORDING → SCORING → RESULT
                                                              └─ "Pick another" → PICKER
```

### PICKER substates

- **Loading**: centered "Loading songs…" text. Shown from `renderPicker()` entry until the fetch resolves.
- **Ready**: scrollable button list. Each row is a two-line button: `name` on top, `singer  •  rhythm` smaller beneath.
- **Error**: message + **Retry** button. Triggered by any HTTP failure, non-200 status, `code != 0`, or JSON parse error.

Fetch runs on `renderPicker()` entry, so returning via `← Songs` always refetches. In-memory session caching is out of scope.

### DOWNLOADING state

`renderDownloading(song)`: the existing `titleRowWithBack("🎵  ${song.name}")` header plus "Downloading song…" subtitle. Nothing else.

A `downloadGeneration: Int` field mirrors the existing `scoringGeneration` pattern. Incrementing it on back-press makes the completion callback no-op. The underlying `HttpURLConnection` read finishes quietly in the background; no interrupt plumbing.

## Networking

- `java.net.HttpURLConnection` on `kotlin.concurrent.thread(isDaemon = true)`. Callbacks posted to the main `Handler` via `main.post`.
- Timeouts: list call — connect 10 s, read 15 s. Zip download — connect 10 s, read 60 s.
- JSON: `org.json.JSONObject` / `JSONArray` (built into Android, no new dep).
- Zip download: stream response body → `cacheDir/songs/<id>.zip.part`, `renameTo(<id>.zip)` on completion. The `.part` suffix prevents a half-written file from being treated as cached on the next run; any stale `.part` found on download entry is deleted first.
- Download is idempotent and always dispatches through the background thread for a uniform callback shape: if `<id>.zip` exists and is non-empty, the worker skips the HTTP request and `main.post`s `onDone` immediately.

## Staging

`SongStaging.stage(ctx, song)` keeps today's semantics: ensure `<id>_chorus.mp3` exists alongside the zip in `cacheDir/songs/<id>/`. Changes vs. today:

- Source of the zip is `cacheDir/songs/<id>.zip` (already downloaded), not `ctx.assets.open(...)`.
- No more `readDisplayName(...)` — the display name now comes from the API response, not the zip's embedded JSON.

## Manifest & network security

- Add `<uses-permission android:name="android.permission.INTERNET"/>`.
- Add `android:networkSecurityConfig="@xml/network_security_config"` on `<application>`.
- `res/xml/network_security_config.xml`:
  ```xml
  <network-security-config>
    <domain-config cleartextTrafficPermitted="true">
      <domain includeSubdomains="false">210.22.95.26</domain>
    </domain-config>
  </network-security-config>
  ```
  HTTPS is still used for CDN zip downloads; cleartext is narrowed to the one IP that needs it.

## Non-goals

- No filter, search, or sort on the picker.
- No pagination UI — first 100 songs only.
- No detail API call — list gives us the zip URL directly.
- No offline fallback to bundled zips — the APK no longer ships them.
- No progress bar during download; "Downloading song…" text only.
- No retry with backoff; a single attempt, then the user hits Retry (picker) or back (download).
- No changes to the SDK, JNI, or C++ core.
- No changes to the C++ tests or their `SongHighlightSamples` fixtures.

## Testing

Manual, on the demo APK:

1. Fresh install, online → picker shows "Loading songs…" briefly, then 100 two-line buttons. Pick a song → "Downloading song…" → PREVIEW starts playing. Zip lands in `cacheDir/songs/<id>.zip`.
2. Second pick of the same song → DOWNLOADING passes through instantly (cache hit) → PREVIEW.
3. Airplane mode before launch → picker shows error + Retry. Toggle online → Retry → list loads.
4. Start a pick → "← Songs" during DOWNLOADING → lands on picker. Re-picking the same song later works (no stuck `.part` file).
5. Full flow end-to-end: pick → download → preview → countdown → record → score → RESULT. Confirm the score path (zip → MIDI → YIN → aggregate) is unchanged.
6. APK size drops (no `SongHighlightSamples` assets).

No new unit/CI tests — this is demo-app wiring; the C++ core surface is untouched.
