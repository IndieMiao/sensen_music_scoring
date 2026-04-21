// Session lifecycle. Real scoring lands in Phase 1c — for now we load the song
// and count frames, then return a floor score.

#include "singscoring.h"

#include <memory>

#include "song.h"

struct ss_session {
    std::unique_ptr<ss::Song> song;
    int samples_received = 0;
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
    (void)samples;
    (void)sample_rate;
    if (!s || n_samples <= 0) return;
    s->samples_received += n_samples;
}

extern "C" int ss_finalize_score(ss_session* s) {
    if (!s) return 10;
    return 10;  // Phase 1c replaces this with real scoring.
}

extern "C" void ss_close(ss_session* s) {
    delete s;
}
