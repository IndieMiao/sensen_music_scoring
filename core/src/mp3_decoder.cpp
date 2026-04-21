#include "mp3_decoder.h"

// Configure minimp3 to emit float32 directly, then pull in its implementation.
#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

namespace ss {

Pcm decode_mp3(const uint8_t* data, size_t size) {
    Pcm out;
    if (!data || size == 0) return out;

    mp3dec_t dec;
    mp3dec_init(&dec);

    const uint8_t* p       = data;
    size_t         remain  = size;
    mp3d_sample_t  frame_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];  // float when MINIMP3_FLOAT_OUTPUT is set

    while (remain > 0) {
        mp3dec_frame_info_t info{};
        int samples_per_ch = mp3dec_decode_frame(
            &dec, p, int(remain), frame_pcm, &info);

        if (info.frame_bytes <= 0) break;

        if (samples_per_ch > 0) {
            if (out.sample_rate == 0) out.sample_rate = info.hz;

            if (info.channels == 1) {
                out.samples.insert(out.samples.end(),
                                   frame_pcm, frame_pcm + samples_per_ch);
            } else {
                // Stereo → mono average.
                out.samples.reserve(out.samples.size() + samples_per_ch);
                for (int i = 0; i < samples_per_ch; ++i) {
                    float l = frame_pcm[2 * i];
                    float r = frame_pcm[2 * i + 1];
                    out.samples.push_back(0.5f * (l + r));
                }
            }
        }

        p      += info.frame_bytes;
        remain -= size_t(info.frame_bytes);
    }

    return out;
}

} // namespace ss
