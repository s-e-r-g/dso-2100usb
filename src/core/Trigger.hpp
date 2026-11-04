#pragma once

// Software edge trigger: finds a level crossing in a captured record.

#include "core/Scope.hpp"

#include <optional>
#include <span>

namespace Core
{

// Returns the fractional sample index of the first edge crossing `levelV` in [from, to).
// The signal must first leave the band levelV +- hysteresisV on the opposite side, so noise
// on a slow edge does not re-trigger. Nothing found: std::nullopt.
std::optional<double> FindTrigger(std::span<const float> s, double levelV, TriggerEdge edge,
                                  double hysteresisV, size_t from, size_t to);

}
