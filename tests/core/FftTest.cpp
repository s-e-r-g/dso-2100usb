// FFT: a sine must show up in the right bin with its RMS level in dBV.

#include "core/Fft.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

// 1 V peak = 0.707 V RMS = -3.01 dBV, in bin 64 of a 1024-point FFT. Checked for the window
// meant for amplitude readings (flat top) and the default (Hann, within its scallop loss).
TEST(Fft, SineLevelAndBin)
{
    constexpr size_t   kN   = 1024;
    constexpr size_t   kBin = 64;
    std::vector<float> s(kN);
    for (size_t i = 0; i < kN; ++i)
    {
        s[i] =
            static_cast<float>(std::sin(2 * std::numbers::pi * kBin * static_cast<double>(i) / kN));
    }
    for (const auto w : {Core::FftWindow::kFlatTop, Core::FftWindow::kHann})
    {
        const auto spec = Core::SpectrumDbv(s, w);
        ASSERT_EQ(spec.size(), kN / 2);
        const auto peak = std::max_element(spec.begin(), spec.end());
        EXPECT_EQ(static_cast<size_t>(peak - spec.begin()), kBin);
        EXPECT_NEAR(*peak, -3.01, w == Core::FftWindow::kFlatTop ? 0.05 : 0.1);
    }
}
