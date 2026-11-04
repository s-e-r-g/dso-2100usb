#pragma once

// Simulated DSO-2100 front end (8-bit ADC, ranges, AC coupling, clipping, free-running
// capture) so the GUI can be developed and shown without the hardware. Acquisition thread only.

#include "hw/ScopeBackend.hpp"

#include <random>

namespace Hw
{

class DemoBackend final : public ScopeBackend
{
public:
    DemoBackend();

    AcquireResult Acquire(const Core::AcqSettings& s, Core::Frame& out,
                          const std::atomic<bool>& abort) override;

private:
    static double Signal(size_t channel, double t);
    static double I2cSignal(size_t channel, double t); // SCL on CH1, SDA on CH2

    std::mt19937 m_rng;
    double       m_timeSec = 0.0;
    bool         m_i2cBus  = false; // DSO_DEMO_I2C=1: simulate an I2C bus
};

}
