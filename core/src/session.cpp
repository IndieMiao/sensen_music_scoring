// Phase 0 stub. Real implementation lands in Phase 1.

#include "singscoring.h"

#include <cstdlib>
#include <string>

struct ss_session {
    std::string zip_path;
    int samples_received = 0;
    int finalized_score = 0;
};

extern "C" ss_session* ss_open(const char* zip_path) {
    if (!zip_path) return nullptr;
    auto* s = new ss_session;
    s->zip_path = zip_path;
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
    // Stub: report the floor until Phase 1 lands the real scorer.
    s->finalized_score = 10;
    return s->finalized_score;
}

extern "C" void ss_close(ss_session* s) {
    delete s;
}
