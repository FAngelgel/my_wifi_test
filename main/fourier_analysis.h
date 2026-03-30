#pragma once

#include <cstddef>

struct FourierPeak
{
    size_t bin = 0;
    float freq_hz = 0.0f;
    float magnitude = 0.0f;
};

struct FourierAnalysisResult
{
    float dc = 0.0f;
    float rms = 0.0f;
    FourierPeak peak{};
};

// Computes a single-sided magnitude spectrum (bins 0..N/2) for a real-valued signal.
//
// - `samples`: real-valued time samples
// - `n`: number of samples (recommended: power of 2, but not required for DFT)
// - `sample_rate_hz`: sampling rate used to map bins to frequency
// - `mag_out`: output magnitude spectrum
// - `mag_out_len`: must be >= (n / 2 + 1)
// - `result`: optional summary (DC, RMS, peak bin/frequency/magnitude)
// - `remove_dc`: subtract mean before analysis
// - `apply_hann_window`: apply Hann window before DFT
//
// Notes:
// - Magnitudes are not amplitude-calibrated; they are suitable for comparing bins and finding peaks.
bool fourier_analyze_real_dft(const float *samples,
                              size_t n,
                              float sample_rate_hz,
                              float *mag_out,
                              size_t mag_out_len,
                              FourierAnalysisResult *result,
                              bool remove_dc = true,
                              bool apply_hann_window = true);

