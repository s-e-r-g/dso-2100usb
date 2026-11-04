#pragma once

// I2C decoder working on captured samples: START, address, data, ACK/NACK and STOP.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Core
{

enum class I2cKind : uint8_t
{
    kStart,
    kRepeatedStart,
    kStop,
    kAddress, // value: 7-bit address, read: the R/W bit
    kData,
};

struct I2cEvent
{
    I2cKind kind  = I2cKind::kStart;
    size_t  begin = 0; // sample index
    size_t  end   = 0;
    uint8_t value = 0;
    bool    read  = false;
    bool    ack   = false;
};

// Decodes the record. The logic threshold is taken from each signal itself, so any logic level
// works; a line that does not swing by at least 0.5 V gives no events. Decoding starts at the
// first START: bytes before it have no known bit position, and a byte cut off by the end of the
// record is dropped.
std::vector<I2cEvent> DecodeI2c(std::span<const float> scl, std::span<const float> sda);

// Which of two lines is SCL: 0 or 1, by decoding both ways and keeping the one with more acknowledged
// bytes, since the wrong way round gives next to none. nullopt: no traffic to tell by.
std::optional<size_t> DetectI2cClock(std::span<const float> a, std::span<const float> b);

// One line per transaction, for the log: "+12.40µs  S 3C W A  00 A  AE A  P".
// dt: seconds per sample; times are relative to sample triggerIndex.
std::string FormatI2cLog(std::span<const I2cEvent> events, double dt, double triggerIndex);

}
