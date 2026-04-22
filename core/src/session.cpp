// Session lifecycle: accumulate user PCM, and on finalize run YIN + per-note scoring.

#include "singscoring.h"

#include <cmath>
#include <memory>
#include <vector>

#include "mp3_decoder.h"
#include "pitch_detector.h"
#include "scorer.h"
#include "song.h"

#ifdef __ANDROID__
#include <android/log.h>
#define SS_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ss-core", __VA_ARGS__)
#else
#define SS_LOGI(...) ((void)0)
#endif

struct ss_session {
    std::unique_ptr<ss::Song> song;
    std::vector<float>        pcm;              // accumulated user samples (mono)
    int                       sample_rate = 0;  // locked on first non-empty feed
    bool                      finalized   = false;
};

extern "C" ss_session* ss_open(const char* zip_path) {
    if (!zip_path) return nullptr;
    auto song = ss::load_song(zip_path);
    if (!song) return nullptr;
    auto* s = new ss_session;
    s->song = std::move(song);
    return s;
}

extern "C" void ss_feed_pcm(ss_session* s, const float* samples, int n_samples, int sample_rate) {
    if (!s || !samples || n_samples <= 0 || sample_rate <= 0) return;
    if (s->finalized) return;

    if (s->sample_rate == 0) {
        s->sample_rate = sample_rate;
    } else if (sample_rate != s->sample_rate) {
        // Inconsistent capture rate mid-session. Drop rather than guess — resampling
        // is Phase 2+ concern once we know what real microphones produce.
        return;
    }
    s->pcm.insert(s->pcm.end(), samples, samples + n_samples);
}

extern "C" int ss_finalize_score(ss_session* s) {
    if (!s || !s->song) return 10;
    s->finalized = true;

    if (s->pcm.empty() || s->sample_rate <= 0 || s->song->notes.empty()) {
        SS_LOGI("finalize: short-circuit floor (pcm=%zu rate=%d notes=%zu)",
                s ? s->pcm.size() : 0,
                s ? s->sample_rate : 0,
                (s && s->song) ? s->song->notes.size() : 0);
        return 10;
    }

    // PCM peak/RMS so we can tell silent-mic vs real audio.
    double peak = 0.0, sumsq = 0.0;
    for (float v : s->pcm) {
        double a = v < 0 ? -double(v) : double(v);
        if (a > peak) peak = a;
        sumsq += double(v) * double(v);
    }
    double rms = s->pcm.empty() ? 0.0 : std::sqrt(sumsq / s->pcm.size());

    auto frames = ss::detect_pitches(
        s->pcm.data(), int(s->pcm.size()), s->sample_rate);

    size_t n_voiced = 0;
    double first_voiced_ms = -1.0, last_voiced_ms = -1.0;
    for (const auto& f : frames) {
        if (f.voiced()) {
            if (first_voiced_ms < 0) first_voiced_ms = f.time_ms;
            last_voiced_ms = f.time_ms;
            ++n_voiced;
        }
    }

    const auto& notes = s->song->notes;
    double first_note_start = notes.front().start_ms;
    double first_note_end   = notes.front().end_ms;
    double last_note_start  = notes.back().start_ms;
    double last_note_end    = notes.back().end_ms;

    auto per_note = ss::score_notes(notes, frames);
    int  score    = ss::aggregate_score(notes, per_note);

    SS_LOGI("finalize: pcm=%zu rate=%d durMs=%.0f peak=%.4f rms=%.4f "
            "frames=%zu voiced=%zu voicedSpan=[%.0f..%.0f] "
            "notes=%zu firstNote=[%.0f..%.0f] lastNote=[%.0f..%.0f] score=%d",
            s->pcm.size(), s->sample_rate,
            double(s->pcm.size()) * 1000.0 / s->sample_rate,
            peak, rms,
            frames.size(), n_voiced, first_voiced_ms, last_voiced_ms,
            notes.size(), first_note_start, first_note_end,
            last_note_start, last_note_end,
            score);

    return score;
}

extern "C" void ss_close(ss_session* s) {
    delete s;
}

extern "C" int ss_score(const char* zip_path,
                        const float* samples, int n_samples, int sample_rate) {
    ss_session* s = ss_open(zip_path);
    if (!s) return 10;
    ss_feed_pcm(s, samples, n_samples, sample_rate);
    int score = ss_finalize_score(s);
    ss_close(s);
    return score;
}

extern "C" long long ss_melody_end_ms(const char* zip_path) {
    if (!zip_path) return -1;
    auto song = ss::load_song(zip_path);
    if (!song || song->notes.empty()) return -1;
    return static_cast<long long>(song->melody_end_ms());
}
