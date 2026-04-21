#ifndef SINGSCORING_MP3_DECODER_H
#define SINGSCORING_MP3_DECODER_H

#include <cstdint>
#include <vector>

namespace ss {

struct Pcm {
    std::vector<float> samples;   // mono float32 in [-1, 1], downmixed if source was stereo
    int                sample_rate = 0;

    bool empty() const { return samples.empty(); }
    double duration_ms() const {
        return sample_rate > 0 ? (double(samples.size()) / sample_rate * 1000.0) : 0.0;
    }
};

// Decode raw MP3 bytes into mono float PCM at the source's native sample rate.
// Returns an empty Pcm on failure (malformed MP3, no decoded frames, etc).
Pcm decode_mp3(const uint8_t* data, size_t size);

} // namespace ss

#endif
