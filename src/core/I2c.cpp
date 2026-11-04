// I2C decoder: digitise both lines with hysteresis, then follow the bus rules. SDA changing while
// SCL is high is START or STOP; otherwise SDA is read on each rising edge of SCL.

#include "core/I2c.hpp"

#include "core/Format.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace Core
{

namespace
{

constexpr float kMinSwingV   = 0.5F;
constexpr float kHysteresis  = 0.15F; // share of the swing on each side of the middle
constexpr int   kBitsPerByte = 8;

// Turns a line into logic levels; the level holds while the signal is between the thresholds.
class Digitizer
{
public:
    explicit Digitizer(std::span<const float> s)
    {
        const auto [lo, hi] = std::minmax_element(s.begin(), s.end());
        m_valid             = lo != s.end() && *hi - *lo >= kMinSwingV;
        if (m_valid)
        {
            const float mid  = (*lo + *hi) / 2;
            const float band = (*hi - *lo) * kHysteresis;
            m_high           = mid + band;
            m_low            = mid - band;
            m_level          = s.front() > mid;
        }
    }

    bool Valid() const { return m_valid; }

    bool Next(float v)
    {
        if (v >= m_high)
        {
            m_level = true;
        }
        else if (v <= m_low)
        {
            m_level = false;
        }
        return m_level;
    }

private:
    bool  m_valid = false;
    bool  m_level = true;
    float m_high  = 0;
    float m_low   = 0;
};

}

std::vector<I2cEvent> DecodeI2c(std::span<const float> scl, std::span<const float> sda)
{
    std::vector<I2cEvent> events;
    const size_t          n = std::min(scl.size(), sda.size());
    if (n < 2)
    {
        return events;
    }
    Digitizer clock(scl.first(n));
    Digitizer data(sda.first(n));
    if (!clock.Valid() || !data.Valid())
    {
        return events;
    }

    bool    prevScl    = clock.Next(scl[0]);
    bool    prevSda    = data.Next(sda[0]);
    bool    inTransfer = false; // a START was seen
    bool    nextIsAddr = false;
    int     bits       = 0;
    uint8_t shift      = 0;
    size_t  byteBegin  = 0;

    for (size_t i = 1; i < n; ++i)
    {
        const bool c = clock.Next(scl[i]);
        const bool d = data.Next(sda[i]);

        if (c && prevScl && d != prevSda)
        {
            if (!d) // falling SDA with SCL high
            {
                I2cEvent e;
                e.kind  = inTransfer ? I2cKind::kRepeatedStart : I2cKind::kStart;
                e.begin = i;
                e.end   = i;
                events.push_back(e);
                inTransfer = true;
                nextIsAddr = true;
            }
            else if (inTransfer)
            {
                I2cEvent e;
                e.kind  = I2cKind::kStop;
                e.begin = i;
                e.end   = i;
                events.push_back(e);
                inTransfer = false;
            }
            bits  = 0;
            shift = 0;
        }
        else if (inTransfer && c && !prevScl)
        {
            if (bits == 0)
            {
                byteBegin = i;
            }
            if (bits < kBitsPerByte)
            {
                shift = static_cast<uint8_t>((shift << 1U) | (d ? 1U : 0U));
                ++bits;
            }
            else // the ninth clock: low SDA acknowledges
            {
                I2cEvent e;
                e.begin = byteBegin;
                e.end   = i;
                e.ack   = !d;
                if (nextIsAddr)
                {
                    e.kind     = I2cKind::kAddress;
                    e.value    = static_cast<uint8_t>(shift >> 1U);
                    e.read     = (shift & 1U) != 0;
                    nextIsAddr = false;
                }
                else
                {
                    e.kind  = I2cKind::kData;
                    e.value = shift;
                }
                events.push_back(e);
                bits  = 0;
                shift = 0;
            }
        }
        prevScl = c;
        prevSda = d;
    }
    return events;
}

std::optional<size_t> DetectI2cClock(std::span<const float> a, std::span<const float> b)
{
    const auto acked = [](const std::vector<I2cEvent>& events)
    {
        return std::count_if(
            events.begin(), events.end(), [](const I2cEvent& e)
            { return e.ack && (e.kind == I2cKind::kAddress || e.kind == I2cKind::kData); });
    };
    const auto first  = acked(DecodeI2c(a, b));
    const auto second = acked(DecodeI2c(b, a));
    if (first == second)
    {
        return std::nullopt;
    }
    return first > second ? 0 : 1;
}

std::string FormatI2cLog(std::span<const I2cEvent> events, double dt, double triggerIndex)
{
    std::string out;
    const auto  time = [&](const I2cEvent& e)
    {
        const double t = (static_cast<double>(e.begin) - triggerIndex) * dt;
        return (t >= 0 ? "+" : "") + FormatSi(t, "s");
    };
    const auto byte = [](unsigned v)
    {
        std::array<char, 4> buf{};
        std::snprintf(buf.data(), buf.size(), "%02X", v);
        return std::string(buf.data());
    };
    bool lineOpen = false;
    for (const I2cEvent& e : events)
    {
        switch (e.kind)
        {
        case I2cKind::kStart:
            out += (lineOpen ? "\n" : "") + time(e) + "  S";
            lineOpen = true;
            break;
        case I2cKind::kRepeatedStart: out += "  Sr"; break;
        case I2cKind::kStop:          out += "  P"; break;
        case I2cKind::kAddress:
            out += "  " + byte(e.value) + (e.read ? " R" : " W") + (e.ack ? " A" : " N");
            break;
        case I2cKind::kData: out += "  " + byte(e.value) + (e.ack ? " A" : " N"); break;
        }
    }
    return out;
}

}
