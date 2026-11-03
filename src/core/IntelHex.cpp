// Intel HEX parser: data, EOF and extended address records; start address records are ignored.

#include "core/IntelHex.hpp"

#include <charconv>
#include <stdexcept>

namespace Core
{

namespace
{

constexpr uint8_t kRecData        = 0x00;
constexpr uint8_t kRecEof         = 0x01;
constexpr uint8_t kRecExtSegment  = 0x02;
constexpr uint8_t kRecExtLinear   = 0x04;
constexpr size_t  kRecordOverhead = 5; // count, address hi/lo, type, checksum

[[noreturn]] void Fail(int lineNo, const char* what)
{
    throw std::runtime_error("HEX line " + std::to_string(lineNo) + ": " + what);
}

std::vector<uint8_t> DecodeLine(std::string_view line, int lineNo)
{
    if (line.size() < 1 + 2 * kRecordOverhead || line.front() != ':' || (line.size() - 1) % 2 != 0)
    {
        Fail(lineNo, "bad record format");
    }
    std::vector<uint8_t> bytes;
    bytes.reserve((line.size() - 1) / 2);
    for (size_t i = 1; i < line.size(); i += 2)
    {
        uint8_t     b   = 0;
        const char* beg = line.data() + i;
        const auto  res = std::from_chars(beg, beg + 2, b, 16);
        if (res.ec != std::errc{} || res.ptr != beg + 2)
        {
            Fail(lineNo, "bad hex digit");
        }
        bytes.push_back(b);
    }
    if (bytes.size() != bytes[0] + kRecordOverhead)
    {
        Fail(lineNo, "length mismatch");
    }
    uint8_t sum = 0;
    for (const uint8_t b : bytes)
    {
        sum = static_cast<uint8_t>(sum + b);
    }
    if (sum != 0)
    {
        Fail(lineNo, "checksum error");
    }
    return bytes;
}

}

std::vector<HexSegment> ParseIntelHex(std::string_view text)
{
    std::vector<HexSegment> segments;
    uint32_t                base   = 0;
    int                     lineNo = 0;
    while (!text.empty())
    {
        const size_t     eol  = text.find('\n');
        std::string_view line = text.substr(0, eol);
        text.remove_prefix(eol == std::string_view::npos ? text.size() : eol + 1);
        ++lineNo;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        {
            line.remove_suffix(1);
        }
        if (line.empty())
        {
            continue;
        }

        const std::vector<uint8_t> rec  = DecodeLine(line, lineNo);
        const uint32_t             addr = (static_cast<uint32_t>(rec[1]) << 8U) | rec[2];
        const uint8_t              type = rec[3];
        const auto                 data = rec.begin() + 4;
        const auto                 end  = data + rec[0];

        switch (type)
        {
        case kRecData:
        {
            const uint32_t full = base + addr;
            if (!segments.empty() && segments.back().address + segments.back().data.size() == full)
            {
                segments.back().data.insert(segments.back().data.end(), data, end);
            }
            else
            {
                segments.push_back({full, std::vector<uint8_t>(data, end)});
            }
            break;
        }
        case kRecEof:        return segments;
        case kRecExtSegment: base = ((static_cast<uint32_t>(rec[4]) << 8U) | rec[5]) << 4U; break;
        case kRecExtLinear:  base = ((static_cast<uint32_t>(rec[4]) << 8U) | rec[5]) << 16U; break;
        default:             break;
        }
    }
    return segments;
}

}
