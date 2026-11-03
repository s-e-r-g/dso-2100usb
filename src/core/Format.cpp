// Engineering-notation formatting with SI prefixes from G down to p.

#include "core/Format.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Core
{

namespace
{

struct Prefix
{
    double      scale;
    const char* symbol;
};

constexpr auto kPrefixes = std::to_array<Prefix>({
    {1e9, "G"},
    {1e6, "M"},
    {1e3, "k"},
    {1.0, ""},
    {1e-3, "m"},
    {1e-6, "µ"},
    {1e-9, "n"},
    {1e-12, "p"},
});

constexpr double kRoundingMargin = 0.9995; // 999.96 m prints as 1.000, not 1000 m

}

std::string FormatSi(double value, std::string_view unit, int digits)
{
    if (!std::isfinite(value))
    {
        return "----";
    }
    const double  mag = std::fabs(value);
    const Prefix* p   = &kPrefixes[3];
    if (mag > 0)
    {
        p = &kPrefixes.back();
        for (const Prefix& pr : kPrefixes)
        {
            if (mag >= pr.scale * kRoundingMargin)
            {
                p = &pr;
                break;
            }
        }
    }
    const double scaled   = value / p->scale;
    const double sm       = std::fabs(scaled);
    const int    intDigit = sm >= 99.995 ? 3 : sm >= 9.9995 ? 2 : 1;
    const int    decimals = std::max(0, digits - intDigit);

    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "%.*f", decimals, scaled);
    return std::string(buf.data()) + p->symbol + std::string(unit);
}

}
