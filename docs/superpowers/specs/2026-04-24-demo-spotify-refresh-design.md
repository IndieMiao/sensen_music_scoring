# Demo Spotify-style refresh, song/artist filter, version footer

## Goal

Make the Android demo more attractive as a showcase and more usable at the current catalog size (2000+ songs, growing). Three bundled changes:

1. **Spotify-inspired visual refresh** on the two screens the user actually looks at — the song picker and the result screen. Other screens (preview, countdown, recording, scoring) inherit the new dark palette without further redesign.
2. **Song/artist filter** — single search box above the list, case-insensitive substring match across song name *and* singer.
3. **Version footer** on the picker — `Demo <x.y.z> · SDK <x.y.z>` to make it obvious which build is running.

Not in scope: redesigning the recording/lyrics UI, RecyclerView item animations, theming via XML, dark/light toggle, persisted filter state across launches, unit/instrumented tests for the picker.

## Why now

At ~500 songs the existing `ScrollView`-of-`LinearLayout` picker worked. At 2000+ the eager inflation is visible — slow first paint, rebuild-on-filter would be worse. Moving to `RecyclerView` is the right Android pattern at this scale. Bundling that structural change with a visual refresh is cheaper than doing them as two passes because both touch the same `renderPicker*` code paths.

## Architecture

No new SDK surface; no changes to `core/` or the JNI. Entirely inside `demo-android/`. The demo continues its "programmatic UI, one file per screen, no XML layouts" convention. New code is isolated to a small `ui/` package:

```
demo-android/src/main/kotlin/com/sensen/singscoring/demo/
  MainActivity.kt          (modified — picker + result render functions)
  ui/
    Palette.kt             (new — color + dp helpers)
    SongListAdapter.kt     (new — RecyclerView.Adapter)
    ScoreRingView.kt       (new — FrameLayout that draws a circular ring around a centered TextView child)
```

## Visual language

A single `Palette` object holds the dark-mode constants. The existing `Theme.Material3.DayNight.NoActionBar` is kept so ripple/selector defaults still work; backgrounds are overridden at view-construction time.

- **Background:** `#121212` (screen), `#181818` (rows/cards).
- **Text:** `#FFFFFF` primary, `#B3B3B3` secondary, `#6A6A6A` tertiary (footer).
- **Accent green (pass / primary CTA):** `#1DB954`.
- **Fail red (score < 60 on result only):** `#F15E6C`.
- **Corners:** 12dp on rows and cards; 24dp on the primary pill button.
- **Type scale:** 26sp screen title, 22sp result song title, 18sp row title, 16sp body, 13sp row subtitle / header subtitle, 11sp footer.
- **Status bar:** colored `#121212` via `window.statusBarColor`, with light icons via `WindowInsetsControllerCompat(window, window.decorView).isAppearanceLightStatusBars = false` (false = appearance is *not* light → icons render light-on-dark).

Light-mode behavior: the activity now always renders dark regardless of the system setting. That's a one-way commitment for this demo and acceptable — the Spotify reference *is* dark-first.

## Picker screen

Layout top-to-bottom inside the existing `FrameLayout` root:

1. **Header** — `SingScoring` (26sp white) over `Pick a song` (13sp `#B3B3B3`).
2. **Search bar** — single-line `EditText` inside a rounded `#282828` container (`GradientDrawable`, 24dp corners). Hint: `Search songs or artists`. Leading search glyph rendered as a left `TextView` (🔍) to avoid extra drawable assets.
3. **Song list** — `RecyclerView` with vertical `LinearLayoutManager`. `layout_weight = 1` so it fills the remaining space above the footer.
4. **Version footer** — centered `TextView`, 11sp `#6A6A6A`, `"Demo ${BuildConfig.VERSION_NAME} · SDK ${SingScoringSession.version}"`. `buildFeatures { buildConfig = true }` added to `demo-android/build.gradle.kts` (required on AGP 8+).

### Filter

Search wiring:

- `EditText.addTextChangedListener` posts a `Runnable` 150ms in the future, canceling any prior pending one (`Handler.removeCallbacks`). The runnable calls `adapter.setQuery(newText)`.
- Match rule: trim the query; if empty, show all songs. Otherwise: `song.name.contains(q, ignoreCase = true) || song.singer.contains(q, ignoreCase = true)`.
- Adapter keeps two fields: `allSongs: List<Song>` and `filtered: List<Song>`. `setQuery` recomputes `filtered` and calls `notifyDataSetChanged()`. No `DiffUtil` — the cost of a full rebind on a recycled list of ~10 visible rows is trivial.
- While the catalog is loading (before the `fetchAll` callback resolves), the search bar renders but is disabled (`isEnabled = false`, alpha 0.5).

### Empty-match state

When `filtered.isEmpty()` *and* the query is non-empty: show a muted centered `TextView` (`No songs match "${query}"`, 13sp `#B3B3B3`) in the RecyclerView's place. Implementation: wrap the RecyclerView and the empty-state `TextView` in a `FrameLayout`; toggle `visibility` based on `filtered.isEmpty()` in a small controller block in `renderPickerReady`.

### Row styling

One row = vertical `LinearLayout` in a `FrameLayout` for the selection indicator:

- Row container: 12dp corner radius via a `GradientDrawable` background (solid `#181818`), 12dp vertical padding, 20dp horizontal padding. Foreground is `?attr/selectableItemBackground` so the Material ripple renders on top of the dark row fill.
- Title: 18sp white.
- Subtitle: 13sp `#B3B3B3`, formatted `singer  •  rhythm` (same joiner as today, unchanged logic).
- **Last-picked highlight:** instead of the current amber fill, a 3dp vertical `#1DB954` bar on the left edge. The `Adapter` takes `highlightedId: String?`; setter compares against each bound row's id and toggles a `View.VISIBLE` / `INVISIBLE` on the bar view.

### Scroll preservation

Replace the existing `pickerScrollView` + `pickerScrollY` fields with a single `pickerListState: Parcelable?`. Save on `onSongPicked` via `recyclerView.layoutManager?.onSaveInstanceState()`; restore in `renderPickerReady` after setting the adapter via `layoutManager.onRestoreInstanceState(saved)`.

## Result screen

Layout top-to-bottom, all centered horizontally except the back button:

1. **Back button** — text-style clickable `TextView` (`← Songs`, 16sp `#B3B3B3`, 24dp padding), top-left. Replaces the current `Button("← Songs")`. Rest of `titleRowWithBack` is no longer used on result.
2. **Song title + singer** — centered stack. Title 22sp white; singer 13sp `#B3B3B3` from `pendingSong.singer`.
3. **Score ring** — the `ScoreRingView` (see below). Shows the currently-displayed number (respects `showRemapped`). Color: `#1DB954` when displayed ≥ 60, `#F15E6C` otherwise.
4. **Pass label** — 16sp. Copy unchanged: `Passed` (green) / `Needs work (pass ≥ 60)` (red).
5. **Toggle button** — outlined style: transparent fill, 1.5dp `#535353` border via a stateful `GradientDrawable`, white text. Copy unchanged: `Show raw score` / `Show new score`.
6. **Primary CTA** — filled pill: `#1DB954` background, black text, 24dp corners, bold. Copy unchanged: `Pick another song`.

### ScoreRingView

A `FrameLayout` subclass added in `ui/ScoreRingView.kt`. Responsibilities:

- Holds a single child `TextView` (the score number, 96sp, centered).
- Overrides `dispatchDraw` to draw a 2dp circular stroke whose color matches the text, centered, inset by half the stroke width so the ring doesn't clip. Diameter is `min(width, height) - 16dp`.
- Public setter `setColor(@ColorInt c)` updates both the child `TextView.textColor` and the paint color, then calls `invalidate()`.

Sizing: fixed square via `LinearLayout.LayoutParams(ringSize, ringSize)` where `ringSize = 200dp`. Keeps implementation trivial; no need for `onMeasure` work.

### Toggle behavior

Unchanged from the existing `drawResultBody`: tapping the outlined button flips `showRemapped`, recomputes `displayed = if (showRemapped) remapScore(raw) else raw`, recomputes `passed = displayed >= 60`, and re-renders via `drawResultBody(song)`. The 59→60 remap flip (red to green) documented in the current code is preserved as-is.

## Files touched

**New:**

- `demo-android/src/main/kotlin/com/sensen/singscoring/demo/ui/Palette.kt` — color constants, one `Int.dp(context)` extension.
- `demo-android/src/main/kotlin/com/sensen/singscoring/demo/ui/SongListAdapter.kt` — `RecyclerView.Adapter<RowViewHolder>` with `setSongs` / `setQuery` / `setHighlightedId`, click callback.
- `demo-android/src/main/kotlin/com/sensen/singscoring/demo/ui/ScoreRingView.kt` — `FrameLayout` that draws a colored ring around its `TextView` child.

**Modified:**

- `demo-android/build.gradle.kts` — add `buildFeatures { buildConfig = true }` and `implementation(libs.androidx.recyclerview)`.
- `gradle/libs.versions.toml` — add `androidx-recyclerview = "1.3.2"` + alias.
- `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`:
  - Rewrite `renderPicker`, `renderPickerReady`, `renderPickerError` for dark palette + search bar + RecyclerView + version footer.
  - Delete the existing `songButton(...)` method (its role moves into the adapter).
  - Rewrite `drawResultBody` for the ring-centered result layout.
  - Restyle `titleView` / `subtitleView` / `titleRowWithBack` for dark palette so preview / countdown / recording / scoring inherit the new look without structural changes.
  - Replace `pickerScrollView` + `pickerScrollY` fields with `pickerListState: Parcelable?`.
  - In `onCreate`: set `window.statusBarColor = 0xFF121212.toInt()` and `WindowInsetsControllerCompat(window, window.decorView).isAppearanceLightStatusBars = false`.

**Unchanged:**

- `AudioRecorder.kt`, `LrcParser.kt`, `LyricsScrollView.kt`, `SongStaging.kt`, `SongCatalog.kt`, the Android manifest, `themes.xml`, the SDK module, `core/`, JNI, iOS bindings.

## Edge cases

- **Catalog still loading when user types:** search bar disabled until `renderPickerReady` fires (see *Filter*).
- **Empty catalog / API error:** existing `renderPickerError` path, re-skinned to dark palette; Retry button becomes the green pill style.
- **Query with no matches:** muted empty-state message (see *Empty-match state*).
- **Activity recreation (rotation):** not handled today, not added here. The activity rebuilds state from scratch on recreate; that behavior is unchanged.
- **Fast taps on a row while scrolling:** RecyclerView click handlers behave the same as the current LinearLayout taps — no new debounce needed.

## Version sourcing

- Demo: `BuildConfig.VERSION_NAME` — generated from the `versionName` in `demo-android/build.gradle.kts` once `buildConfig = true` is set.
- SDK: `SingScoringSession.version` — already exposed by the binding (referenced today in the loading subtitle).

Both are pulled at render time inside `renderPicker`. No caching; both are constants.

## Manual test checklist

This demo has no instrumented tests today; adding them for a cosmetic rewrite is disproportionate. Validation is manual:

1. **Catalog load** — launch, catalog fetches, list shows 2000+ rows and scrolls smoothly (no jank past the first screenful).
2. **Filter matches by song name** — type `love`; list narrows within ~150ms.
3. **Filter matches by singer** — type a known singer substring; matching songs appear.
4. **Filter clears** — delete all characters; full list returns.
5. **Filter empty-state** — type `zzzzz`; empty-state message appears; deleting restores the list.
6. **Song pick → full flow** — tap a song, go through preview / countdown / recording / result without regression.
7. **Scroll position preserved** — scroll to row ~500, pick a song, return via `← Songs`; list returns to the same offset with that row green-highlighted.
8. **Result ring — pass color** — get a score ≥ 60; ring and number are green; `Passed` label matches.
9. **Result ring — fail color** — get a displayed score < 60; ring and number are red; label matches.
10. **Score toggle** — on result, tap `Show raw score`; number and ring recolor correctly if the displayed value crosses 60.
11. **Version footer** — confirm `Demo 0.1.0 · SDK 0.4.0` matches `build.gradle.kts` + `singscoring_version.h`.
12. **Status bar** — matches the dark app background on the picker (no white bleed above the header).

## Open questions

None at design time. Implementation details (exact dp values for padding, choice of icon glyph vs drawable for the search affordance) to be decided during execution.
