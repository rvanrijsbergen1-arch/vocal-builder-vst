// Small, dependency-free DSP helpers used by the engine:
// a windowed-sinc resampler and a Hann STFT/iSTFT pair.
#pragma once
#include <cmath>
#include <complex>
#include <vector>

namespace vb::dsp
{
// High-quality resampling (Kaiser-windowed sinc, ~100 dB stopband).
// Works on a single channel; length of the result is round(n * outRate / inRate).
std::vector<float> resample (const std::vector<float>& in, double inRate, double outRate);

// Real FFT wrapper (power-of-two sizes), implemented with an iterative radix-2 FFT.
class RealFFT
{
public:
    explicit RealFFT (int size);
    int size() const { return n; }
    // in: n real samples -> out: n/2+1 complex bins
    void forward (const float* in, std::complex<float>* out);
    // in: n/2+1 complex bins -> out: n real samples (scaled by 1/n)
    void inverse (const std::complex<float>* in, float* out);

private:
    int n;
    std::vector<std::complex<float>> twiddle, buf;
    std::vector<int> bitrev;
    void fft (std::complex<float>* data, bool inverse);
};

// Periodic Hann window (matches tf.signal.hann_window(periodic=True)).
std::vector<float> hannPeriodic (int n);

// RMS in dB over the active (non-silent) part of a signal.
double activeRmsDb (const float* x, size_t n);
} // namespace vb::dsp
