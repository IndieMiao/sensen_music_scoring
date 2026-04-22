// Session lifecycle: accumulate user PCM, and on finalize run YIN + per-note scoring.

#include "singscoring.h"

#include <memory>
#include <vector>

#include "mp3_decoder.h"
#include "pitch_detector.h"
#include "scorer.h"
#include "song.h"

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

    if (s->pcm.empty() || s->sample_rate <= 0 || s->song->notes.empty()) return 10;

    auto frames = ss::detect_pitches(
        s->pcm.data(), int(s->pcm.size()), s->sample_rate);
    auto per_note = ss::score_notes(s->song->notes, frames);
    return ss::aggregate_score(s->song->notes, per_note);
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
