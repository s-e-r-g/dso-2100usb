// dso2100diag: console check of the DSO-2100 without the GUI. Captures a few records and prints
// raw ADC statistics per channel, so a wrong buffer layout or dead inputs show up at once.
//
//   dso2100diag [captures] [range 0..6] [rate 0..13] [dump.bin]
//
// The scope must already run the firmware (start the GUI once after plugging it in).

#include "hw/Dso2100Usb.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <span>

namespace
{

void Stats(const char* name, std::span<const uint8_t> s)
{
    if (s.empty())
    {
        std::printf("  %s: empty\n", name);
        return;
    }
    double sum = 0;
    double sq  = 0;
    auto   lo  = s[0];
    auto   hi  = s[0];
    std::map<uint8_t, size_t> hist;
    for (const uint8_t v : s)
    {
        sum += v;
        sq += static_cast<double>(v) * v;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
        ++hist[v];
    }
    const double n    = static_cast<double>(s.size());
    const double mean = sum / n;
    const double sd   = std::sqrt(std::max(0.0, sq / n - mean * mean));
    // edges through the middle of the range: a clocked signal gives many
    const double mid   = (lo + hi) / 2.0;
    size_t       edges = 0;
    for (size_t i = 1; i < s.size(); ++i)
    {
        if ((s[i - 1] < mid) != (s[i] < mid))
        {
            ++edges;
        }
    }
    std::printf("  %s: min %3u max %3u mean %6.2f sd %5.2f distinct %zu edges %zu\n", name, lo, hi,
                mean, sd, hist.size(), edges);
    std::printf("     first:");
    for (size_t i = 0; i < std::min<size_t>(s.size(), 24); ++i)
    {
        std::printf(" %02x", s[i]);
    }
    std::printf("\n");
}

}

int main(int argc, char* argv[])
{
    const int    captures = argc > 1 ? std::atoi(argv[1]) : 3;
    const size_t range    = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 3;
    const size_t rate     = argc > 3 ? std::strtoul(argv[3], nullptr, 10) : 0;
    const char*  dumpPath = argc > 4 ? argv[4] : nullptr;

    Hw::Dso2100Usb usb;
    if (!usb.Init())
    {
        std::printf("libusb: %s\n", usb.LastError().c_str());
        return 1;
    }
    const Hw::DeviceProbe p = usb.Probe();
    std::printf("bootloader(1006) %s, running(1002) %s, open error: %s\n",
                p.bootloaderPresent ? "yes" : "no", p.runningPresent ? "yes" : "no",
                p.openError.empty() ? "none" : p.openError.c_str());
    if (!usb.Open())
    {
        std::printf("open: %s\n", usb.LastError().c_str());
        return 1;
    }

    std::printf("connected at %s\n", usb.UsbSpeed().c_str());

    Core::AcqSettings s;
    s.rateIndex = std::min(rate, Core::kRateDividers.size() - 1);
    for (Core::ChannelSettings& ch : s.ch)
    {
        ch.vdivIndex = std::min(range, Core::kHwVoltsPerDiv.size() - 1);
    }
    std::printf("range %.2f V/div (x1), rate %.0f Sa/s\n", Core::kHwVoltsPerDiv.at(s.ch[0].vdivIndex),
                Core::kBaseSampleRateHz / Core::kRateDividers.at(s.rateIndex));

    const std::atomic<bool> never{false};
    for (int i = 0; i < captures; ++i)
    {
        Core::Frame f;
        const auto  r = usb.Acquire(s, f, never);
        if (r != Hw::AcquireResult::kOk)
        {
            std::printf("capture %d failed: %s\n", i, usb.LastError().c_str());
            return 1;
        }
        std::printf("capture %d: buffer %zu bytes\n", i, usb.LastRawBuffer().size());
        std::printf("  time: arm %.2f  wait %.2f  usb-read %.2f ms\n", f.timing.armMs,
                    f.timing.waitMs, f.timing.readMs);
        Stats("CH1", f.raw[0]);
        Stats("CH2", f.raw[1]);
    }

    // the whole buffer in quarters, to spot the real layout if the offsets are wrong
    const auto buf = usb.LastRawBuffer();
    for (size_t q = 0; q < 4; ++q)
    {
        char name[32];
        std::snprintf(name, sizeof name, "buf[%04zx..]", q * buf.size() / 4);
        Stats(name, buf.subspan(q * buf.size() / 4, buf.size() / 4));
    }
    if (dumpPath != nullptr)
    {
        std::ofstream(dumpPath, std::ios::binary)
            .write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        std::printf("raw buffer written to %s\n", dumpPath);
    }
    return 0;
}
