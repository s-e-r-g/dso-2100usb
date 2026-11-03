// Frame -> screen pipeline. The screen window is placed around the software trigger point;
// the record is long enough (PickRateIndex) to leave room on both sides.

#include "app/Processor.hpp"

#include "core/Trigger.hpp"

#include <algorithm>
#include <cmath>

namespace App
{

namespace
{

constexpr double kTriggerHysteresisDiv = 0.2;
constexpr double kHalfScreenDiv        = Core::kHDivCount / 2.0;

}

std::optional<DisplayData> Processor::Process(const Core::Frame& f, const ViewSettings& v)
{
    const size_t n = f.Len();
    if (n < 2)
    {
        return std::nullopt;
    }
    const double fs           = f.sampleRateHz;
    const double perDiv       = v.secPerDiv * fs; // samples per division
    const double screenLen    = perDiv * Core::kHDivCount;
    const double startFromTrg = v.delaySec * fs - kHalfScreenDiv * perDiv; // screen start - trigger

    DisplayData d;
    d.seq          = f.seq;
    d.sampleRateHz = fs;
    d.demo         = f.demo;

    // ---------- trigger ----------
    std::optional<double> trg = v.heldTriggerIndex;
    if (!trg && v.trig.source != Core::TriggerSource::kExt)
    {
        const auto                   src = static_cast<size_t>(v.trig.source);
        const Core::ChannelSettings& ch  = f.ch.at(src);
        const auto                   lo  = static_cast<size_t>(std::max(1.0, -startFromTrg));
        const auto                   hi  = static_cast<size_t>(std::clamp(
            static_cast<double>(n) - (startFromTrg + screenLen), 0.0, static_cast<double>(n)));
        trg = Core::FindTrigger(f.volts.at(src), v.trig.levelV, v.trig.edge,
                                ch.VoltsPerDiv() * kTriggerHysteresisDiv, lo, hi);
    }
    else if (!trg && f.hwTriggered)
    {
        trg = std::max(0.0, -startFromTrg); // the hardware trigger point is not reported
    }
    d.triggered = trg.has_value();
    if (!trg)
    {
        if (v.trig.mode != Core::TriggerMode::kAuto)
        {
            return std::nullopt;
        }
        trg = std::clamp(-startFromTrg, 0.0, static_cast<double>(n)); // untriggered: record start
    }
    d.triggerIndex     = *trg;
    const double start = *trg + startFromTrg;

    // ---------- visible window ----------
    const auto i0 =
        static_cast<size_t>(std::clamp(std::floor(start) - 1, 0.0, static_cast<double>(n)));
    const auto i1 = static_cast<size_t>(
        std::clamp(std::ceil(start + screenLen) + 2, 0.0, static_cast<double>(n)));
    const double x0Div = (static_cast<double>(i0) - start) / perDiv;

    const bool resetAvg = v.averages <= 1 || f.settingsGen != m_avgGen ||
                          v.secPerDiv != m_avgSecPerDiv || v.delaySec != m_avgDelay ||
                          m_avg[0].size() != i1 - i0;
    if (resetAvg)
    {
        m_avgCount     = 0;
        m_avgGen       = f.settingsGen;
        m_avgSecPerDiv = v.secPerDiv;
        m_avgDelay     = v.delaySec;
    }
    const int avgN = std::min(m_avgCount + 1, std::max(v.averages, 1));

    for (size_t c = 0; c < Core::kChannelCount; ++c)
    {
        Trace& t = d.ch.at(c);
        t.valid  = f.ch.at(c).enabled && i1 > i0;
        t.x0Div  = x0Div;
        t.dxDiv  = 1.0 / perDiv;
        t.v.assign(f.volts.at(c).begin() + static_cast<std::ptrdiff_t>(i0),
                   f.volts.at(c).begin() + static_cast<std::ptrdiff_t>(i1));
        if (v.averages > 1)
        {
            std::vector<float>& avg = m_avg.at(c);
            if (m_avgCount == 0)
            {
                avg = t.v;
            }
            else
            {
                for (size_t i = 0; i < avg.size(); ++i)
                {
                    avg[i] += (t.v[i] - avg[i]) / static_cast<float>(avgN);
                }
            }
            t.v = avg;
        }
    }
    if (v.averages > 1)
    {
        ++m_avgCount;
    }

    // ---------- measurements: on screen, like bench scopes ----------
    const auto onScreen = [&](const Trace& t) -> std::span<const float>
    {
        const auto first = static_cast<size_t>(std::max(0.0, std::ceil(-t.x0Div / t.dxDiv)));
        const auto last  = std::min(
            t.v.size(),
            static_cast<size_t>(std::max(0.0, (Core::kHDivCount - t.x0Div) / t.dxDiv + 1)));
        if (first >= last)
        {
            return {};
        }
        return std::span<const float>(t.v).subspan(first, last - first);
    };
    for (size_t c = 0; c < Core::kChannelCount; ++c)
    {
        if (d.ch.at(c).valid)
        {
            d.meas.at(c) = Core::MeasureAll(onScreen(d.ch.at(c)), f.Dt());
        }
        else
        {
            d.meas.at(c).fill(std::nan(""));
        }
    }

    // ---------- math ----------
    d.mathMeas.fill(std::nan(""));
    if (v.math == MathOp::kFft)
    {
        d.spectrumDbv = Core::SpectrumDbv(f.volts.at(v.mathA), v.fftWindow);
        d.spectrumBinHz =
            d.spectrumDbv.empty() ? 0.0 : fs / (2.0 * static_cast<double>(d.spectrumDbv.size()));
    }
    else if (v.math != MathOp::kOff)
    {
        const Trace& a = d.ch.at(v.mathA);
        const Trace& b = d.ch.at(v.mathB);
        d.math         = a;
        d.math.valid   = true;
        for (size_t i = 0; i < d.math.v.size(); ++i)
        {
            switch (v.math)
            {
            case MathOp::kAdd: d.math.v[i] = a.v[i] + b.v[i]; break;
            case MathOp::kSub: d.math.v[i] = a.v[i] - b.v[i]; break;
            case MathOp::kMul: d.math.v[i] = a.v[i] * b.v[i]; break;
            default:           break;
            }
        }
        d.mathMeas = Core::MeasureAll(onScreen(d.math), f.Dt());
    }
    return d;
}

}
