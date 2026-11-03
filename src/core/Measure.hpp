#pragma once

// Automatic waveform measurements (the Rigol/R&S "Measure" menu).

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace Core
{

enum class MeasureKind : uint8_t
{
    kFreq,
    kPeriod,
    kVpp,
    kVmax,
    kVmin,
    kVamp,
    kVtop,
    kVbase,
    kVavg,
    kVrms,
    kPosWidth,
    kNegWidth,
    kDuty,
    kRise,
    kFall,
    kOvershoot,
    kCount,
};

constexpr size_t kMeasureCount = static_cast<size_t>(MeasureKind::kCount);

// Display name and unit of each measurement, indexed by MeasureKind.
struct MeasureInfo
{
    std::string_view name;
    std::string_view unit;
};
const MeasureInfo& GetMeasureInfo(MeasureKind kind);

// All measurements of one trace; a value that cannot be measured is NaN.
using MeasureValues = std::array<double, kMeasureCount>;

// Measures the samples `s` taken every `dt` seconds.
MeasureValues MeasureAll(std::span<const float> s, double dt);

}
