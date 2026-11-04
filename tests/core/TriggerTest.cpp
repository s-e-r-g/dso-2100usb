// Software trigger: edge direction, interpolation and hysteresis.

#include "core/Trigger.hpp"

#include <gtest/gtest.h>

#include <vector>

// The crossing between two samples must be interpolated, so the trace does not jitter by a
// whole sample from capture to capture.
TEST(Trigger, RisingEdgeInterpolated)
{
    const std::vector<float> s = {0, 0, 0, 1, 2, 2};
    const auto t = Core::FindTrigger(s, 0.5, Core::TriggerEdge::kRising, 0.1, 0, s.size());
    ASSERT_TRUE(t.has_value());
    EXPECT_DOUBLE_EQ(*t, 2.5);
}

// A falling trigger must ignore the rising edge that comes first.
TEST(Trigger, FallingSkipsRising)
{
    const std::vector<float> s = {0, 2, 2, 0, 0};
    const auto t = Core::FindTrigger(s, 1.0, Core::TriggerEdge::kFalling, 0.1, 0, s.size());
    ASSERT_TRUE(t.has_value());
    EXPECT_DOUBLE_EQ(*t, 2.5);
}

// Noise around the level that never leaves the hysteresis band must not trigger.
TEST(Trigger, HysteresisRejectsNoise)
{
    const std::vector<float> s = {1.0F, 1.05F, 0.97F, 1.04F, 0.98F, 1.03F};
    EXPECT_FALSE(Core::FindTrigger(s, 1.0, Core::TriggerEdge::kRising, 0.2, 0, s.size()));
}
