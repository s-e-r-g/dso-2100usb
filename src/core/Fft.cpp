// Iterative radix-2 FFT (no recursion) with window correction so a sine of amplitude A
// reads A/sqrt(2) V RMS in its bin whatever window is chosen.

#include "core/Fft.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <numbers>

namespace Core
{

namespace
{

constexpr size_t kMinLen   = 16;
constexpr size_t kMaxLen   = 1U << 16U;
constexpr double kFloorDbv = -160.0;
constexpr auto   kWindowNames =
    std::to_array<std::string_view>({"Rectangle", "Hann", "Hamming", "Blackman", "Flat top"});

double WindowAt(FftWindow w, size_t i, size_t n)
{
    const double x = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n - 1);
    switch (w)
    {
    case FftWindow::kRectangle: return 1.0;
    case FftWindow::kHann:      return 0.5 - 0.5 * std::cos(x);
    case FftWindow::kHamming:   return 0.54 - 0.46 * std::cos(x);
    case FftWindow::kBlackman:  return 0.42 - 0.5 * std::cos(x) + 0.08 * std::cos(2 * x);
    case FftWindow::kFlatTop:
        return 0.21557895 - 0.41663158 * std::cos(x) + 0.277263158 * std::cos(2 * x) -
               0.083578947 * std::cos(3 * x) + 0.006947368 * std::cos(4 * x);
    }
    return 1.0;
}

void Fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1U;
        for (; (j & bit) != 0; bit >>= 1U)
        {
            j ^= bit;
        }
        j ^= bit;
        if (i < j)
        {
            std::swap(a[i], a[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1U)
    {
        const double               ang = -2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k]                     = u + v;
                a[i + k + len / 2]           = u - v;
                w *= wl;
            }
        }
    }
}

}

std::string_view FftWindowName(FftWindow w)
{
    return kWindowNames.at(static_cast<size_t>(w));
}

std::vector<float> SpectrumDbv(std::span<const float> s, FftWindow window)
{
    if (s.size() < kMinLen)
    {
        return {};
    }
    size_t n = kMinLen;
    while (n * 2 <= s.size() && n * 2 <= kMaxLen)
    {
        n *= 2;
    }

    std::vector<std::complex<double>> a(n);
    double                            gain = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = WindowAt(window, i, n);
        gain += w;
        // DC stays in: bin 0 shows the offset, as on bench scopes
        a[i] = {s[i] * w, 0.0};
    }
    Fft(a);

    std::vector<float> out(n / 2);
    for (size_t k = 0; k < n / 2; ++k)
    {
        // single-sided amplitude, then peak -> RMS (DC bin is not doubled and has no RMS factor)
        double amp = std::abs(a[k]) / gain;
        amp        = k == 0 ? amp : amp * 2.0 / std::numbers::sqrt2;
        out[k] =
            static_cast<float>(amp > 0 ? std::max(20.0 * std::log10(amp), kFloorDbv) : kFloorDbv);
    }
    return out;
}

}
