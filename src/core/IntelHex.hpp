#pragma once

// Intel HEX parser for the EZ-USB firmware image.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Core
{

// A contiguous block of bytes for one target address.
struct HexSegment
{
    uint32_t             address = 0;
    std::vector<uint8_t> data;
};

// Parses an Intel HEX image; adjacent records are merged into one segment.
// Throws std::runtime_error with the line number on a malformed file.
std::vector<HexSegment> ParseIntelHex(std::string_view text);

}
