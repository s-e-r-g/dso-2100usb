#pragma once

// Spectrum of a trace for the FFT math function.

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Core
{

enum class FftWindow : uint8_t
{
    kRectangle,
    kHann,
    kHamming,
    kBlackman,
    kFlatTop,
};

std::string_view FftWindowName(FftWindow w);

// Amplitude spectrum in dBV (RMS) of the first power-of-two samples of `s`.
// Bin k is k * sampleRate / (2 * result.size()). Fewer than 16 samples: empty result.
std::vector<float> SpectrumDbv(std::span<const float> s, FftWindow window);

}
