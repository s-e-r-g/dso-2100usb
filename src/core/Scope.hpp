#pragma once

// DSO-2100 hardware model and the acquisition settings and frames shared by all layers.
// Portable: standard library only.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Core
{

constexpr size_t kChannelCount     = 2;
constexpr double kBaseSampleRateHz = 50e6;  // ADC clock; the sample rate is this / divider
constexpr size_t kRecordLen        = 30000; // samples per channel kept from each capture
constexpr double kAdcZero          = 127.0; // nominal raw value for 0 V before calibration
constexpr double kCountsPerDiv     = 32.0;  // 256 ADC counts span the 8 vertical divisions
constexpr int    kHDivCount        = 10;
constexpr int    kVDivCount        = 8;

// Sample-rate dividers the firmware accepts (rate = 50 MHz / divider).
constexpr auto kRateDividers =
    std::to_array<int>({1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000});

// Hardware vertical ranges in V/div at the BNC (x1 probe). The index is sent to the device.
constexpr auto kHwVoltsPerDiv = std::to_array<double>({0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0});

constexpr auto kProbeChoices = std::to_array<int>({1, 10, 100});

// Raw ADC value read for 0 V, one per hardware range (filled by the calibration).
using ZeroCal = std::array<double, kHwVoltsPerDiv.size()>;

constexpr ZeroCal MakeZeroCal()
{
    ZeroCal z{};
    z.fill(kAdcZero);
    return z;
}

enum class Coupling : uint8_t
{
    kDc,
    kAc,
    kGnd,
};

enum class TriggerSource : uint8_t
{
    kCh1,
    kCh2,
    kExt,
};

enum class TriggerEdge : uint8_t
{
    kRising,
    kFalling,
};

enum class TriggerMode : uint8_t
{
    kAuto,
    kNormal,
    kSingle,
};

struct ChannelSettings
{
    bool     enabled   = true;
    size_t   vdivIndex = 4; // index into kHwVoltsPerDiv
    Coupling coupling  = Coupling::kDc;
    int      probe     = 1;
    bool     invert    = false;
    ZeroCal  zeroCal   = MakeZeroCal();

    double VoltsPerDiv() const { return kHwVoltsPerDiv.at(vdivIndex) * probe; }
};

struct TriggerSettings
{
    TriggerMode   mode            = TriggerMode::kAuto;
    TriggerSource source          = TriggerSource::kCh1;
    TriggerEdge   edge            = TriggerEdge::kRising;
    double        levelV          = 0.0; // software trigger level at the probe tip
    // Let the scope wait for its own trigger. Always on for EXT, which only the hardware sees.
    bool          hardwareTrigger = false;
};

struct AcqSettings
{
    std::array<ChannelSettings, kChannelCount> ch;
    TriggerSettings                            trig;
    size_t                                     rateIndex = 6; // index into kRateDividers
};

// One capture. raw comes from the backend; volts has probe, inversion and calibration applied.
struct Frame
{
    uint64_t seq          = 0;
    uint64_t settingsGen  = 0; // AcqSettings it was taken with
    double   sampleRateHz = kBaseSampleRateHz;
    std::array<std::vector<uint8_t>, kChannelCount> raw;
    std::array<std::vector<float>, kChannelCount>   volts;
    std::array<ChannelSettings, kChannelCount>      ch;
    TriggerSettings                                 trig;
    bool                                            demo        = false;
    bool                                            hwTriggered = false;

    double Dt() const { return 1.0 / sampleRateHz; }
    size_t Len() const { return volts[0].size(); }
};

// The time/div choices offered to the user (1-2-5 steps, 50 ns .. 1 s).
std::vector<double> TimePerDivChoices();

// The fastest sample rate whose 10 divisions fill at most half the record, so the trigger
// can be found with room on both sides. Returns an index into kRateDividers.
size_t PickRateIndex(double secPerDiv);

// Fills f.volts from f.raw using the channel settings stored in the frame.
void ConvertToVolts(Frame& f);

}
