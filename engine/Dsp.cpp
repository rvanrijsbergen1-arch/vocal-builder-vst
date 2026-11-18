#include "Dsp.h"
#include <algorithm>
#include <numeric>

namespace vb::dsp
{
static constexpr double kPi = 3.14159265358979323846;

static double besselI0 (double x)
{
    double sum = 1.0, term = 1.0, k = 1.0;
    const double q = x * x / 4.0;
    while (term > 1e-12 * sum)
    {
        term *= q / (k * k);
        sum += term;
        k += 1.0;
    }
    return sum;
}

std::vector<float> resample (const std::vector<float>& in, double inRate, double outRate)
{
    if (in.empty())
        return {};
    if (std::abs (inRate - outRate) < 1e-9)
        return in;

    const double ratio = outRate / inRate;
    const size_t outLen = (size_t) std::llround ((double) in.size() * ratio);
    std::vector<float> out (outLen);

    // Cut-off at the lower Nyquist with a small guard band.
    const double cutoff = 0.97 * std::min (1.0, ratio);
    const int halfTaps = 32; // taps on each side, in units of the lower rate
    const double beta = 9.0; // Kaiser beta (~90 dB)
    const double i0b = besselI0 (beta);
    const double step = std::min (1.0, ratio); // kernel scale
    const int reach = (int) std::ceil (halfTaps / step);

    // Precompute a finely sampled kernel table for speed.
    const int tableRes = 512;
    const int tableLen = halfTaps * tableRes + 1;
    std::vector<float> table ((size_t) tableLen);
    for (int i = 0; i < tableLen; ++i)
    {
        const double x = (double) i / tableRes; // in units of the kernel (lower rate samples)
        const double sincArg = kPi * x * cutoff;
        const double sinc = x == 0.0 ? 1.0 : std::sin (sincArg) / sincArg;
        const double r = x / halfTaps;
        const double w = r >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - r * r)) / i0b;
        table[(size_t) i] = (float) (sinc * w * cutoff);
    }

    const long long nIn = (long long) in.size();
    for (size_t o = 0; o < outLen; ++o)
    {
        const double centre = (double) o / ratio;
        const long long c = (long long) std::floor (centre);
        double acc = 0.0;
        for (long long k = c - reach + 1; k <= c + reach; ++k)
        {
            if (k < 0 || k >= nIn)
                continue;
            const double d = std::abs (centre - (double) k) * step; // distance in kernel units
            if (d >= halfTaps)
                continue;
            const double pos = d * tableRes;
            const int i0 = (int) pos;
            const double frac = pos - i0;
            const double kv = table[(size_t) i0] + (i0 + 1 < tableLen ? (table[(size_t) i0 + 1] - table[(size_t) i0]) * frac : 0.0);
            acc += in[(size_t) k] * kv;
        }
        out[o] = (float) acc;
    }
    return out;
}

RealFFT::RealFFT (int size) : n (size), twiddle ((size_t) size / 2), buf ((size_t) size), bitrev ((size_t) size)
{
    for (int i = 0; i < n / 2; ++i)
        twiddle[(size_t) i] = std::polar (1.0f, (float) (-2.0 * kPi * i / n));
    int bits = 0;
    while ((1 << bits) < n)
        ++bits;
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        bitrev[(size_t) i] = r;
    }
}

void RealFFT::fft (std::complex<float>* data, bool inv)
{
    for (int i = 0; i < n; ++i)
        if (i < bitrev[(size_t) i])
            std::swap (data[i], data[bitrev[(size_t) i]]);
    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2, stride = n / len;
        for (int i = 0; i < n; i += len)
            for (int j = 0; j < half; ++j)
            {
                auto w = twiddle[(size_t) (j * stride)];
                if (inv)
                    w = std::conj (w);
                const auto u = data[i + j];
                const auto v = data[i + j + half] * w;
                data[i + j] = u + v;
                data[i + j + half] = u - v;
            }
    }
}

void RealFFT::forward (const float* in, std::complex<float>* out)
{
    for (int i = 0; i < n; ++i)
        buf[(size_t) i] = { in[i], 0.0f };
    fft (buf.data(), false);
    for (int i = 0; i <= n / 2; ++i)
        out[i] = buf[(size_t) i];
}

void RealFFT::inverse (const std::complex<float>* in, float* out)
{
    for (int i = 0; i <= n / 2; ++i)
        buf[(size_t) i] = in[i];
    for (int i = n / 2 + 1; i < n; ++i)
        buf[(size_t) i] = std::conj (in[n - i]);
    fft (buf.data(), true);
    const float s = 1.0f / (float) n;
    for (int i = 0; i < n; ++i)
        out[i] = buf[(size_t) i].real() * s;
}

// ------------------------------------------------------------------ AnyFFT
AnyFFT::AnyFFT (int size) : n (size)
{
    pow2 = (n & (n - 1)) == 0;
    m = 1;
    while (m < (pow2 ? n : 2 * n - 1))
        m <<= 1;
    twm.resize ((size_t) m / 2);
    for (int i = 0; i < m / 2; ++i)
        twm[(size_t) i] = std::polar (1.0, -2.0 * kPi * i / m);
    int bits = 0;
    while ((1 << bits) < m)
        ++bits;
    brm.resize ((size_t) m);
    for (int i = 0; i < m; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        brm[(size_t) i] = r;
    }
    work.resize ((size_t) m);
    if (! pow2)
    {
        chirp.resize ((size_t) n);
        for (int k = 0; k < n; ++k)
        {
            const long long k2 = ((long long) k * k) % (2LL * n); // exact phase index
            chirp[(size_t) k] = std::polar (1.0, -kPi * (double) k2 / n);
        }
        bfft.assign ((size_t) m, {});
        bfft[0] = std::conj (chirp[0]);
        for (int k = 1; k < n; ++k)
            bfft[(size_t) k] = bfft[(size_t) (m - k)] = std::conj (chirp[(size_t) k]);
        radix2 (bfft.data(), false);
    }
}

void AnyFFT::radix2 (std::complex<double>* d, bool inv)
{
    for (int i = 0; i < m; ++i)
        if (i < brm[(size_t) i])
            std::swap (d[i], d[brm[(size_t) i]]);
    for (int len = 2; len <= m; len <<= 1)
    {
        const int half = len / 2, stride = m / len;
        for (int i = 0; i < m; i += len)
            for (int j = 0; j < half; ++j)
            {
                auto w = twm[(size_t) (j * stride)];
                if (inv)
                    w = std::conj (w);
                const auto u = d[i + j], v = d[i + j + half] * w;
                d[i + j] = u + v;
                d[i + j + half] = u - v;
            }
    }
}

void AnyFFT::forward (std::complex<double>* data)
{
    if (pow2)
    {
        radix2 (data, false);
        return;
    }
    std::fill (work.begin(), work.end(), std::complex<double> {});
    for (int k = 0; k < n; ++k)
        work[(size_t) k] = data[k] * chirp[(size_t) k];
    radix2 (work.data(), false);
    for (int k = 0; k < m; ++k)
        work[(size_t) k] *= bfft[(size_t) k];
    radix2 (work.data(), true);
    const double s = 1.0 / m;
    for (int k = 0; k < n; ++k)
        data[k] = work[(size_t) k] * s * chirp[(size_t) k];
}

void AnyFFT::forwardReal (const float* in, std::complex<float>* out)
{
    std::vector<std::complex<double>> b ((size_t) std::max (n, m));
    for (int i = 0; i < n; ++i)
        b[(size_t) i] = { in[i], 0.0 };
    forward (b.data());
    for (int i = 0; i <= n / 2; ++i)
        out[i] = std::complex<float> ((float) b[(size_t) i].real(), (float) b[(size_t) i].imag());
}

void AnyFFT::inverseReal (const std::complex<float>* in, float* out)
{
    // ifft(X) = conj(fft(conj(X))) / n
    std::vector<std::complex<double>> b ((size_t) std::max (n, m));
    for (int i = 0; i <= n / 2; ++i)
        b[(size_t) i] = std::conj (std::complex<double> (in[i].real(), in[i].imag()));
    for (int i = n / 2 + 1; i < n; ++i)
        b[(size_t) i] = std::complex<double> (in[n - i].real(), in[n - i].imag()); // conj of the mirrored conj
    forward (b.data());
    const double s = 1.0 / n;
    for (int i = 0; i < n; ++i)
        out[i] = (float) (b[(size_t) i].real() * s);
}

std::vector<float> hannPeriodic (int n)
{
    std::vector<float> w ((size_t) n);
    for (int i = 0; i < n; ++i)
        w[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / n));
    return w;
}

double activeRmsDb (const float* x, size_t n)
{
    const size_t win = 2048;
    if (n < win)
    {
        double s = 0;
        for (size_t i = 0; i < n; ++i)
            s += (double) x[i] * x[i];
        return 10.0 * std::log10 (s / std::max<size_t> (1, n) + 1e-12);
    }
    std::vector<double> db;
    for (size_t b = 0; b + win <= n; b += win)
    {
        double s = 0;
        for (size_t i = b; i < b + win; ++i)
            s += (double) x[i] * x[i];
        db.push_back (10.0 * std::log10 (s / win + 1e-12));
    }
    const double mx = *std::max_element (db.begin(), db.end());
    double acc = 0;
    int cnt = 0;
    for (double d : db)
        if (d > mx - 40.0)
        {
            acc += std::pow (10.0, d / 10.0);
            ++cnt;
        }
    return 10.0 * std::log10 (acc / std::max (1, cnt));
}
} // namespace vb::dsp
