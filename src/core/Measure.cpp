// Automatic measurements. Top/base come from the histogram (like bench scopes do), so the
// ringing on a square wave does not count as amplitude; edges are found at 10/50/90 %.

#include "core/Measure.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Core
{

namespace
{

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

constexpr size_t kHistBins        = 128;
constexpr double kModeMinFraction = 0.05; // a histogram mode below this share is not a level
constexpr double kHysteresis      = 0.2;  // of the amplitude, around the 50 % level
constexpr double kLowRef          = 0.1;
constexpr double kHighRef         = 0.9;
constexpr double kMinAmplitudeV   = 1e-6;

// clang-format off
constexpr auto kInfo = std::to_array<MeasureInfo>({
    {"Freq",   "Hz"},
    {"Period", "s"},
    {"Vpp",    "V"},
    {"Vmax",   "V"},
    {"Vmin",   "V"},
    {"Vamp",   "V"},
    {"Vtop",   "V"},
    {"Vbase",  "V"},
    {"Vavg",   "V"},
    {"Vrms",   "V"},
    {"+Width", "s"},
    {"-Width", "s"},
    {"Duty",   "%"},
    {"Rise",   "s"},
    {"Fall",   "s"},
    {"Overshoot", "%"},
});
// clang-format on
static_assert(kInfo.size() == kMeasureCount);

double& At(MeasureValues& v, MeasureKind k)
{
    return v.at(static_cast<size_t>(k));
}

// Fractional index where the segment s[i-1] -> s[i] crosses `level`.
double Crossing(std::span<const float> s, size_t i, double level)
{
    const double a = s[i - 1];
    const double b = s[i];
    return static_cast<double>(i - 1) + (b != a ? (level - a) / (b - a) : 0.0);
}

// Most common level in [lo, hi) of the histogram, or NaN when no level dominates.
double HistMode(const std::vector<size_t>& hist, size_t lo, size_t hi, double minV, double binW,
                size_t total)
{
    size_t best = lo;
    for (size_t b = lo; b < hi; ++b)
    {
        if (hist[b] > hist[best])
        {
            best = b;
        }
    }
    if (static_cast<double>(hist[best]) < kModeMinFraction * static_cast<double>(total))
    {
        return kNan;
    }
    return minV + (static_cast<double>(best) + 0.5) * binW;
}

double Mean(const std::vector<double>& v)
{
    if (v.empty())
    {
        return kNan;
    }
    double sum = 0;
    for (const double x : v)
    {
        sum += x;
    }
    return sum / static_cast<double>(v.size());
}

}

const MeasureInfo& GetMeasureInfo(MeasureKind kind)
{
    return kInfo.at(static_cast<size_t>(kind));
}

MeasureValues MeasureAll(std::span<const float> s, double dt)
{
    MeasureValues out;
    out.fill(kNan);
    const size_t n = s.size();
    if (n < 2)
    {
        return out;
    }

    // ---------- amplitude ----------
    double minV  = s[0];
    double maxV  = s[0];
    double sum   = 0;
    double sumSq = 0;
    for (const float x : s)
    {
        minV = std::min(minV, static_cast<double>(x));
        maxV = std::max(maxV, static_cast<double>(x));
        sum += x;
        sumSq += static_cast<double>(x) * x;
    }
    At(out, MeasureKind::kVmax) = maxV;
    At(out, MeasureKind::kVmin) = minV;
    At(out, MeasureKind::kVpp)  = maxV - minV;
    At(out, MeasureKind::kVavg) = sum / static_cast<double>(n);
    At(out, MeasureKind::kVrms) = std::sqrt(sumSq / static_cast<double>(n));

    double top  = maxV;
    double base = minV;
    if (maxV - minV > kMinAmplitudeV)
    {
        std::vector<size_t> hist(kHistBins, 0);
        const double        binW = (maxV - minV) / kHistBins;
        for (const float x : s)
        {
            const auto b = static_cast<size_t>((x - minV) / binW);
            ++hist[std::min(b, kHistBins - 1)];
        }
        const double t = HistMode(hist, kHistBins / 2, kHistBins, minV, binW, n);
        const double b = HistMode(hist, 0, kHistBins / 2, minV, binW, n);
        top            = std::isnan(t) ? maxV : t;
        base           = std::isnan(b) ? minV : b;
    }
    const double amp             = top - base;
    At(out, MeasureKind::kVtop)  = top;
    At(out, MeasureKind::kVbase) = base;
    At(out, MeasureKind::kVamp)  = amp;
    if (amp <= kMinAmplitudeV)
    {
        return out;
    }
    At(out, MeasureKind::kOvershoot) = (maxV - top) / amp * 100.0;

    // ---------- edges ----------
    const double mid    = base + amp * 0.5;
    const double lowRef = base + amp * kLowRef;
    const double hiRef  = base + amp * kHighRef;
    const double upAt   = mid + amp * kHysteresis / 2;
    const double downAt = mid - amp * kHysteresis / 2;

    std::vector<double> rising;
    std::vector<double> falling;
    std::vector<double> riseTimes;
    std::vector<double> fallTimes;
    int                 state = s[0] > mid ? 1 : -1;
    for (size_t i = 1; i < n; ++i)
    {
        const double v = s[i];
        if (state < 0 && v >= upAt)
        {
            state    = 1;
            size_t j = i;
            while (j > 1 && s[j - 1] >= mid)
            {
                --j;
            }
            rising.push_back(Crossing(s, j, mid));
            size_t lo = j;
            while (lo > 1 && s[lo - 1] >= lowRef)
            {
                --lo;
            }
            size_t hi = j;
            while (hi < n - 1 && s[hi] < hiRef)
            {
                ++hi;
            }
            if (lo > 1 && s[hi] >= hiRef)
            {
                riseTimes.push_back(Crossing(s, hi, hiRef) - Crossing(s, lo, lowRef));
            }
        }
        else if (state > 0 && v <= downAt)
        {
            state    = -1;
            size_t j = i;
            while (j > 1 && s[j - 1] <= mid)
            {
                --j;
            }
            falling.push_back(Crossing(s, j, mid));
            size_t hi = j;
            while (hi > 1 && s[hi - 1] <= hiRef)
            {
                --hi;
            }
            size_t lo = j;
            while (lo < n - 1 && s[lo] > lowRef)
            {
                ++lo;
            }
            if (hi > 1 && s[lo] <= lowRef)
            {
                fallTimes.push_back(Crossing(s, lo, lowRef) - Crossing(s, hi, hiRef));
            }
        }
    }

    const std::vector<double>& ref = rising.size() >= 2 ? rising : falling;
    if (ref.size() >= 2)
    {
        const double period = (ref.back() - ref.front()) / static_cast<double>(ref.size() - 1) * dt;
        At(out, MeasureKind::kPeriod) = period;
        At(out, MeasureKind::kFreq)   = 1.0 / period;
    }

    std::vector<double> pos;
    std::vector<double> neg;
    for (const double r : rising)
    {
        const auto f = std::upper_bound(falling.begin(), falling.end(), r);
        if (f != falling.end())
        {
            pos.push_back((*f - r) * dt);
        }
    }
    for (const double f : falling)
    {
        const auto r = std::upper_bound(rising.begin(), rising.end(), f);
        if (r != rising.end())
        {
            neg.push_back((*r - f) * dt);
        }
    }
    At(out, MeasureKind::kPosWidth) = Mean(pos);
    At(out, MeasureKind::kNegWidth) = Mean(neg);
    At(out, MeasureKind::kDuty) =
        At(out, MeasureKind::kPosWidth) / At(out, MeasureKind::kPeriod) * 100.0;
    At(out, MeasureKind::kRise) = Mean(riseTimes) * dt;
    At(out, MeasureKind::kFall) = Mean(fallTimes) * dt;
    return out;
}

}
