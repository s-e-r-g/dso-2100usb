// Engineering notation used by every readout.

#include "core/Format.hpp"

#include <gtest/gtest.h>

#include <limits>

// Prefix choice and rounding at the decade boundary (999.96 m must not print as 1000 mV).
TEST(Format, Prefixes)
{
    EXPECT_EQ(Core::FormatSi(1234.0, "Hz"), "1.234kHz");
    EXPECT_EQ(Core::FormatSi(0.5, "V", 3), "500mV");
    EXPECT_EQ(Core::FormatSi(0.99996, "V"), "1.000V");
    EXPECT_EQ(Core::FormatSi(-2e-6, "s", 3), "-2.00µs");
    EXPECT_EQ(Core::FormatSi(0.0, "V", 3), "0.00V");
    EXPECT_EQ(Core::FormatSi(std::numeric_limits<double>::quiet_NaN(), "V"), "----");
}
