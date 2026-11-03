// Frame to screen pipeline: what the user sees must not jump while zooming a stopped capture.

#include "app/Processor.hpp"

#include <gtest/gtest.h>

namespace
{

// 2 V pulses with rising edges at samples 1000, 5000 and 9000, at 50 MS/s.
Core::Frame Pulses()
{
    Core::Frame f;
    f.sampleRateHz = Core::kBaseSampleRateHz;
    for (auto& ch : f.volts)
    {
        ch.assign(Core::kRecordLen, 0.0F);
        for (const size_t edge : {1000U, 5000U, 9000U})
        {
            std::fill_n(ch.begin() + static_cast<std::ptrdiff_t>(edge), 500, 2.0F);
        }
    }
    return f;
}

}

// Zooming in narrows the window, so a new search finds an earlier edge and the picture jumps to
// another part of the record. With the held position the same edge stays on screen.
TEST(Processor, ZoomInStoppedCaptureKeepsTheEdge)
{
    const Core::Frame f = Pulses();
    App::Processor    proc;
    App::ViewSettings v;
    v.trig.levelV = 1.0;
    v.secPerDiv   = 10e-6; // 5000 samples on screen: the first edge in reach is 5000

    const auto wide = proc.Process(f, v);
    ASSERT_TRUE(wide.has_value());
    EXPECT_NEAR(wide->triggerIndex, 4999.5, 1.0);

    v.secPerDiv         = 1e-6;
    const auto searched = proc.Process(f, v);
    ASSERT_TRUE(searched.has_value());
    EXPECT_NEAR(searched->triggerIndex, 999.5, 1.0); // why the position has to be held

    v.heldTriggerIndex = wide->triggerIndex;
    const auto held    = proc.Process(f, v);
    ASSERT_TRUE(held.has_value());
    EXPECT_DOUBLE_EQ(held->triggerIndex, wide->triggerIndex);
}
