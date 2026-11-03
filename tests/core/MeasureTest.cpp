// Automatic measurements on synthetic signals with known answers.

#include "core/Measure.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

namespace
{

double Get(const Core::MeasureValues& m, Core::MeasureKind k)
{
    return m.at(static_cast<size_t>(k));
}

std::vector<float> Square(double freqHz, double fs, size_t n, double lowV, double highV,
                          double duty)
{
    std::vector<float> s(n);
    for (size_t i = 0; i < n; ++i)
    {
        const double ph = std::fmod(static_cast<double>(i) / fs * freqHz, 1.0);
        s[i]            = static_cast<float>(ph < duty ? highV : lowV);
    }
    return s;
}

}

// A 1 kHz, 25 % duty, 0..5 V square: frequency, levels and duty must come out exact,
// since autoset and the trigger 50 % button rely on them.
TEST(Measure, SquareWave)
{
    const double fs = 1e6;
    const auto   s  = Square(1000, fs, 10000, 0.0, 5.0, 0.25);
    const auto   m  = Core::MeasureAll(s, 1.0 / fs);
    EXPECT_NEAR(Get(m, Core::MeasureKind::kFreq), 1000.0, 1.0);
    EXPECT_NEAR(Get(m, Core::MeasureKind::kVtop), 5.0, 0.05);
    EXPECT_NEAR(Get(m, Core::MeasureKind::kVbase), 0.0, 0.05);
    EXPECT_NEAR(Get(m, Core::MeasureKind::kDuty), 25.0, 0.5);
    EXPECT_NEAR(Get(m, Core::MeasureKind::kVpp), 5.0, 1e-6);
}

// RMS of a sine is A/sqrt(2); a classic check that sums are not truncated.
TEST(Measure, SineRms)
{
    const double       fs = 1e6;
    std::vector<float> s(100000);
    for (size_t i = 0; i < s.size(); ++i)
    {
        s[i] = static_cast<float>(
            2.0 * std::sin(2 * std::numbers::pi * 1000 * static_cast<double>(i) / fs));
    }
    const auto m = Core::MeasureAll(s, 1.0 / fs);
    EXPECT_NEAR(Get(m, Core::MeasureKind::kVrms), 2.0 / std::numbers::sqrt2, 0.01);
    EXPECT_NEAR(Get(m, Core::MeasureKind::kFreq), 1000.0, 1.0);
}

// A flat line has no edges: time measurements must be NaN (shown as ----), not garbage.
TEST(Measure, DcHasNoFrequency)
{
    const std::vector<float> s(1000, 1.5F);
    const auto               m = Core::MeasureAll(s, 1e-6);
    EXPECT_TRUE(std::isnan(Get(m, Core::MeasureKind::kFreq)));
    EXPECT_NEAR(Get(m, Core::MeasureKind::kVavg), 1.5, 1e-6);
}
