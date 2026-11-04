// Software edge trigger with hysteresis and linear interpolation between samples.

#include "core/Trigger.hpp"

#include <algorithm>

namespace Core
{

std::optional<double> FindTrigger(std::span<const float> s, double levelV, TriggerEdge edge,
                                  double hysteresisV, size_t from, size_t to)
{
    to = std::min(to, s.size());
    if (from == 0)
    {
        from = 1;
    }
    const double sign  = edge == TriggerEdge::kRising ? 1.0 : -1.0;
    const double level = levelV * sign;
    const double armAt = level - hysteresisV;

    bool armed = false;
    for (size_t i = from - 1; i < to; ++i)
    {
        const double v = s[i] * sign;
        if (v < armAt)
        {
            armed = true;
        }
        else if (armed && v >= level && i >= from)
        {
            const double prev = s[i - 1] * sign;
            const double frac = v != prev ? (level - prev) / (v - prev) : 0.0;
            return static_cast<double>(i - 1) + std::clamp(frac, 0.0, 1.0);
        }
    }
    return std::nullopt;
}

}
