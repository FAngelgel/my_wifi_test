#include "fourier_analysis.h"

#include <cmath>
#include <cstdint>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

float hann_window(size_t i, size_t n)
{
    if (n <= 1)
    {
        return 1.0f;
    }
    // Hann: w[i] = 0.5 * (1 - cos(2*pi*i/(n-1)))
    return 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(n - 1)));
}

} // namespace

bool fourier_analyze_real_dft(const float *samples,
                              size_t n,
                              float sample_rate_hz,
                              float *mag_out,
                              size_t mag_out_len,
                              FourierAnalysisResult *result,
                              bool remove_dc,
                              bool apply_hann_window)
{
    if (samples == nullptr || mag_out == nullptr)
    {
        return false;
    }
    if (n < 2)
    {
        return false;
    }

    const size_t needed_len = (n / 2) + 1;
    if (mag_out_len < needed_len)
    {
        return false;
    }
    if (!(sample_rate_hz > 0.0f))
    {
        return false;
    }

    float mean = 0.0f;
    if (remove_dc)
    {
        for (size_t i = 0; i < n; ++i)
        {
            mean += samples[i];
        }
        mean /= static_cast<float>(n);
    }

    float sum_sq = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
        const float x = samples[i] - mean;
        sum_sq += x * x;
    }

    FourierPeak peak{};

    // Single-sided spectrum for real signals: bins 0..N/2
    for (size_t k = 0; k <= n / 2; ++k)
    {
        float re = 0.0f;
        float im = 0.0f;

        for (size_t i = 0; i < n; ++i)
        {
            float x = samples[i] - mean;
            if (apply_hann_window)
            {
                x *= hann_window(i, n);
            }

            const float phase = kTwoPi * static_cast<float>(k) * static_cast<float>(i) / static_cast<float>(n);
            re += x * std::cos(phase);
            im -= x * std::sin(phase); // e^{-j*phase}
        }

        const float mag = std::sqrt((re * re) + (im * im));
        mag_out[k] = mag;

        // Track peak excluding DC bin by default (bin 0 often dominates).
        if (k > 0 && mag >= peak.magnitude)
        {
            peak.bin = k;
            peak.magnitude = mag;
            peak.freq_hz = (static_cast<float>(k) * sample_rate_hz) / static_cast<float>(n);
        }
    }

    if (result != nullptr)
    {
        result->dc = mean;
        result->rms = std::sqrt(sum_sq / static_cast<float>(n));
        result->peak = peak;
    }

    return true;
}

