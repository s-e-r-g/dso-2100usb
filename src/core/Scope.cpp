// Timebase selection and raw ADC to volts conversion.

#include "core/Scope.hpp"

#include <cmath>

namespace Core
{

namespace
{

constexpr double kMinSecPerDiv = 50e-9;
constexpr double kMaxSecPerDiv = 1.0;

}

std::vector<double> TimePerDivChoices()
{
    constexpr auto      kMantissas = std::to_array<double>({1.0, 2.0, 5.0});
    std::vector<double> steps;
    for (int exp = -9; exp <= 0; ++exp)
    {
        for (const double m : kMantissas)
        {
            const double v = m * std::pow(10.0, exp);
            if (v >= kMinSecPerDiv * 0.999 && v <= kMaxSecPerDiv * 1.001)
            {
                steps.push_back(v);
            }
        }
    }
    return steps;
}

size_t PickRateIndex(double secPerDiv)
{
    const double screenSec = secPerDiv * kHDivCount;
    for (size_t i = 0; i < kRateDividers.size(); ++i)
    {
        const double fs = kBaseSampleRateHz / kRateDividers.at(i);
        if (screenSec * fs <= static_cast<double>(kRecordLen) / 2.0)
        {
            return i;
        }
    }
    return kRateDividers.size() - 1;
}

void ConvertToVolts(Frame& f)
{
    for (size_t c = 0; c < kChannelCount; ++c)
    {
        const ChannelSettings&      ch  = f.ch.at(c);
        const std::vector<uint8_t>& raw = f.raw.at(c);
        std::vector<float>&         out = f.volts.at(c);
        out.resize(raw.size());
        if (ch.coupling == Coupling::kGnd)
        {
            std::fill(out.begin(), out.end(), 0.0F);
            continue;
        }
        const double zero  = ch.zeroCal.at(ch.vdivIndex);
        const double scale = ch.VoltsPerDiv() / kCountsPerDiv * (ch.invert ? -1.0 : 1.0);
        for (size_t i = 0; i < raw.size(); ++i)
        {
            out[i] = static_cast<float>((raw[i] - zero) * scale);
        }
    }
}

}
