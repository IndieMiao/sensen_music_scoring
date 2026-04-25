#ifndef SINGSCORING_RESAMPLER_H
#define SINGSCORING_RESAMPLER_H

#include <vector>

namespace ss {

// Decimate a mono float signal by 2 with a Hann-windowed sinc anti-alias FIR.
// Output length = floor(n_in / 2); output sample rate = input / 2.
//
// Filter passband is ~0 to 0.42 of the input Nyquist, with ≳40 dB rejection
// above the new Nyquist. Designed for vocal pitch analysis (YIN search range
// is well below the new Nyquist), not audio reproduction.
//
// Returns an empty vector for n_in < 2.
std::vector<float> decimate_by_2(const float* in, int n_in);

} // namespace ss

#endif
