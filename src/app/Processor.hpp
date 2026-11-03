#pragma once

// Turns captured frames into what the screen shows: trigger alignment, the visible window,
// averaging, math channel, FFT and measurements. GUI thread; no widgets.

#include "app/Acquisition.hpp"
#include "core/Fft.hpp"
#include "core/Measure.hpp"

#include <optional>
#include <vector>

namespace App
{

enum class MathOp : uint8_t
{
    kOff,
    kAdd,
    kSub,
    kMul,
    kFft,
};

struct ViewSettings
{
    double                secPerDiv = 200e-6;
    double                delaySec  = 0.0; // time of the screen centre after the trigger
    int                   averages  = 1;   // 1: off
    MathOp                math      = MathOp::kOff;
    size_t                mathA     = 0; // source channels of the math function (FFT uses A)
    size_t                mathB     = 1;
    Core::FftWindow       fftWindow = Core::FftWindow::kHann;
    // Trigger as set now: the level is applied in software, so it need not restart a capture.
    Core::TriggerSettings trig;
    // Stopped: keep this trigger position, so zooming stays on the same edge instead of searching
    // the new window for another one.
    std::optional<double> heldTriggerIndex;
};

// Samples placed on the screen: sample i is at x = x0Div + i * dxDiv (0..10 divisions).
struct Trace
{
    std::vector<float> v;
    double             x0Div = 0;
    double             dxDiv = 1;
    bool               valid = false;
};

struct DisplayData
{
    uint64_t                               seq          = 0;
    double                                 sampleRateHz = 0;
    bool                                   triggered    = false;
    bool                                   demo         = false;
    std::array<Trace, Core::kChannelCount> ch;
    Trace                                  math;        // add/sub/mul
    std::vector<float>                     spectrumDbv; // FFT
    double                                 spectrumBinHz = 0;
    double                                 triggerIndex  = 0; // in the frame, fractional
    std::array<Core::MeasureValues, Core::kChannelCount> meas{};
    Core::MeasureValues                                  mathMeas{};
};

class Processor
{
public:
    // nullopt: Normal or Single trigger mode and no trigger in this frame (keep the old screen).
    std::optional<DisplayData> Process(const Core::Frame& f, const ViewSettings& v);

    void ResetAverage() { m_avgCount = 0; }

private:
    // averaging state; reset when anything changes the sample positions
    std::array<std::vector<float>, Core::kChannelCount> m_avg;
    int                                                 m_avgCount     = 0;
    uint64_t                                            m_avgGen       = 0;
    double                                              m_avgSecPerDiv = 0;
    double                                              m_avgDelay     = 0;
};

}
