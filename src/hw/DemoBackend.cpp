// Demo signals: CH1 a 1 kHz 3.3 V logic clock with finite edges and ringing, CH2 a 2.5 kHz
// sine with a 3rd harmonic and slow amplitude wobble. Paced like the real scope.

#include "hw/DemoBackend.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <thread>

namespace Hw
{

namespace
{

constexpr double kClockHz         = 1000.0;
constexpr double kClockHighV      = 3.3;
constexpr double kEdgeTauSec      = 0.6e-6;
constexpr double kRingV           = 0.25;
constexpr double kSineHz          = 2500.0;
constexpr double kSineV           = 1.5;
constexpr double kNoiseDiv        = 0.012; // noise in divisions of the selected range
constexpr double kMaxPaceSec      = 1.5;
constexpr int    kUsbTransferMs   = 25;
constexpr auto   kPollStep        = std::chrono::milliseconds(5);
// simulated ADC offset error per channel, in counts (what the calibration removes)
constexpr auto   kAdcOffsetCounts = std::to_array<double>({0.6, -0.8});

}

DemoBackend::DemoBackend()
    : m_rng(std::random_device{}()), m_i2cBus(std::getenv("DSO_DEMO_I2C") != nullptr)
{
}

double DemoBackend::I2cSignal(size_t channel, double t)
{
    // a 400 kHz write to 0x3C, like the KM board OLED, every 200 µs
    constexpr double kBitSec    = 2.5e-6;
    constexpr double kPeriodSec = 200e-6;
    constexpr double kStartSec  = 10e-6;
    constexpr double kBusV      = 3.3;
    constexpr auto   kBytes     = std::to_array<uint8_t>({0x78, 0x00, 0xAE, 0x40});
    constexpr int    kBitCount  = static_cast<int>(kBytes.size()) * 9;

    const auto bitAt = [&](int k) // SDA during bit k: 8 data bits, then ACK (low)
    {
        const int pos = k % 9;
        return pos < 8 && ((kBytes.at(static_cast<size_t>(k / 9)) >> (7 - pos)) & 1) != 0;
    };
    const double u  = (std::fmod(t, kPeriodSec) - kStartSec) / kBitSec;
    bool         c  = true;
    bool         dv = true;
    if (u >= 0 && u < 1) // START: SDA falls while SCL is high
    {
        c  = u < 0.75;
        dv = u < 0.25;
    }
    else if (u >= 1 && u < kBitCount + 1)
    {
        const int    k     = static_cast<int>(u) - 1;
        const double phase = u - std::floor(u);
        c                  = phase >= 0.5;
        dv                 = phase < 0.1 && k > 0 ? bitAt(k - 1) : bitAt(k);
    }
    else if (u >= kBitCount + 1 && u < kBitCount + 2) // STOP: SDA rises while SCL is high
    {
        const double phase = u - std::floor(u);
        c                  = phase >= 0.25;
        dv                 = phase >= 0.75;
    }
    return (channel == 0 ? c : dv) ? kBusV : 0.0;
}

double DemoBackend::Signal(size_t channel, double t)
{
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    if (channel == 0)
    {
        const double ph   = std::fmod(t * kClockHz, 1.0);
        const double tau  = kEdgeTauSec * kClockHz; // in periods
        const bool   high = ph < 0.5;
        const double x    = (high ? ph : ph - 0.5) / tau;
        const double edge = std::exp(-x);
        const double ring = kRingV * std::exp(-x / 3.0) * std::sin(x * 2.5);
        return high ? kClockHighV * (1.0 - edge) + ring : kClockHighV * edge - ring;
    }
    const double wobble = 1.0 + 0.1 * std::sin(kTwoPi * 0.7 * t);
    return wobble * kSineV * std::sin(kTwoPi * kSineHz * t) +
           0.2 * std::sin(kTwoPi * 3.0 * kSineHz * t + 0.4);
}

AcquireResult DemoBackend::Acquire(const Core::AcqSettings& s, Core::Frame& out,
                                   const std::atomic<bool>& abort)
{
    const double fs        = Core::kBaseSampleRateHz / Core::kRateDividers.at(s.rateIndex);
    const double recordSec = static_cast<double>(Core::kRecordLen) / fs;

    const auto pace = std::chrono::milliseconds(
        static_cast<int>(std::min(recordSec, kMaxPaceSec) * 1000.0) + kUsbTransferMs);
    for (auto waited = std::chrono::milliseconds(0); waited < pace; waited += kPollStep)
    {
        if (abort.load())
        {
            return AcquireResult::kAborted;
        }
        std::this_thread::sleep_for(kPollStep);
    }

    // A free-running capture starts at a random moment, so the trigger has work to do.
    std::uniform_real_distribution<double> jitter(0.0, 0.01);
    std::normal_distribution<double>       noise(0.0, kNoiseDiv);
    m_timeSec += std::chrono::duration<double>(pace).count() + jitter(m_rng);

    std::vector<double> v(Core::kRecordLen);
    for (size_t c = 0; c < Core::kChannelCount; ++c)
    {
        const Core::ChannelSettings& ch     = s.ch.at(c);
        const double                 hwVdiv = Core::kHwVoltsPerDiv.at(ch.vdivIndex);
        double                       mean   = 0;
        for (size_t i = 0; i < v.size(); ++i)
        {
            const double t = m_timeSec + static_cast<double>(i) / fs;
            v[i] = (m_i2cBus ? I2cSignal(c, t) : Signal(c, t)) / ch.probe + noise(m_rng) * hwVdiv;
            mean += v[i];
        }
        mean /= static_cast<double>(v.size());

        std::vector<uint8_t>& raw = out.raw.at(c);
        raw.resize(v.size());
        for (size_t i = 0; i < v.size(); ++i)
        {
            double x = v[i];
            if (ch.coupling == Core::Coupling::kAc)
            {
                x -= mean;
            }
            const double counts =
                Core::kAdcZero + kAdcOffsetCounts.at(c) + x / hwVdiv * Core::kCountsPerDiv;
            raw[i] = static_cast<uint8_t>(std::clamp(std::lround(counts), 0L, 255L));
        }
    }
    out.sampleRateHz = fs;
    out.demo         = true;
    out.hwTriggered  = false;
    return AcquireResult::kOk;
}

}
