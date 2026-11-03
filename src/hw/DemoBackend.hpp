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

    std::mt19937 m_rng;
    double       m_timeSec = 0.0;
};

}
