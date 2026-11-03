// DSO-2100 USB protocol (EZ-USB AN2131 with the vendor firmware), after the oscope2100 project.
//
// Firmware upload (AN21xx): vendor request 0xA0 writes 8051 RAM at wValue; the CPUCS register at
// 0x7F92 holds the CPU in reset (1) while the image is written, then starts it (0).
//
// Running firmware, bulk endpoints:
//   EP4 OUT  8-byte config: 03, gain, (div-1) lo, (div-1) hi, ctrl, 00, trigger source, ff
//            gain: high nibble CH1 range, low nibble CH2 range (codes > 3 are +1),
//                  bit 7 / bit 3: CH1 / CH2 AC coupling
//            ctrl: 0xdf idle; when arming: bit 0 free run, bit 1 falling edge, bit 3 divider > 1
//            trigger source: 1 CH1, 2 CH2, 4 EXT, 0 none
//   EP3 OUT  0x02                 (re)start the capture
//   EP7 OUT  0x1a, EP7 IN 1 byte  capture status, 1 = buffer full
//   EP2 OUT  0x13, EP2 IN 0xff80  samples: CH1 at +2, CH2 at +0x8002
//   EP5 OUT  0x06                 idle (sent at start-up and on close)

#include "hw/Dso2100Usb.hpp"

#include "core/IntelHex.hpp"

#include <libusb.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>

namespace Hw
{

namespace
{

using Clock = std::chrono::steady_clock;

constexpr unsigned kUsbTimeoutMs  = 2000;
constexpr int      kUsbRetryCount = 3;

// firmware upload
constexpr uint8_t  kReqFirmwareLoad = 0xA0;
constexpr uint16_t kAn21CpucsAddr   = 0x7F92;
constexpr size_t   kUploadChunkLen  = 256;
constexpr uint8_t  kCpuReset        = 1;
constexpr uint8_t  kCpuRun          = 0;

// endpoints and commands
constexpr uint8_t kEpSamples     = 2;
constexpr uint8_t kEpCapture     = 3;
constexpr uint8_t kEpConfig      = 4;
constexpr uint8_t kEpControl     = 5;
constexpr uint8_t kEpStatus      = 7;
constexpr uint8_t kCmdReadBuffer = 0x13;
constexpr uint8_t kCmdCapture    = 0x02;
constexpr uint8_t kCmdStatus     = 0x1a;
constexpr uint8_t kCmdIdle       = 0x06;
constexpr uint8_t kStatusFull    = 1;

// config bytes
constexpr uint8_t kCfgHeader   = 0x03;
constexpr uint8_t kCfgTrailer  = 0xff;
constexpr uint8_t kCfgCh1Ac    = 0x80;
constexpr uint8_t kCfgCh2Ac    = 0x08;
constexpr uint8_t kCtrlIdle    = 0xdf;
constexpr uint8_t kCtrlFreeRun = 0x01;
constexpr uint8_t kCtrlFalling = 0x02;
constexpr uint8_t kCtrlDivided = 0x08;
constexpr uint8_t kTrigSrcNone = 0x00;
constexpr uint8_t kTrigSrcCh1  = 0x01;
constexpr uint8_t kTrigSrcCh2  = 0x02;
constexpr uint8_t kTrigSrcExt  = 0x04;
constexpr auto    kInitConfig =
    std::to_array<uint8_t>({0x03, 0xdd, 0x05, 0x00, 0x0f, 0x00, 0x00, 0xff});

// sample buffer
// Always read all of it: after a shorter read the firmware takes no new capture and sends the
// old record again (tested on the hardware).
constexpr size_t kReadLen   = 0xff80;
constexpr size_t kCh1Offset = 0x0002;
constexpr size_t kCh2Offset = 0x8002;

// Auto mode with a hardware trigger gives up after this many record times.
constexpr double kAutoTimeoutRecords = 1.5;
constexpr auto   kAutoTimeoutExtra   = std::chrono::milliseconds(150);
constexpr auto   kFreeRunGrace       = std::chrono::seconds(3);
constexpr double kSlowCaptureSec     = 0.02; // poll gently when a capture takes longer
constexpr auto   kStatusPollPause    = std::chrono::milliseconds(2);

std::string UsbError(int rc)
{
    return libusb_error_name(rc);
}

// DSO_USB_TRACE=1 in the environment: every transfer is written to stderr (protocol debugging).
bool TraceOn()
{
    static const bool s_on = []
    {
        const char* v = std::getenv("DSO_USB_TRACE");
        return v != nullptr && *v != '\0' && *v != '0';
    }();
    return s_on;
}

void Trace(const char* dir, uint8_t ep, std::span<const uint8_t> data, int rc)
{
    if (!TraceOn())
    {
        return;
    }
    constexpr size_t kMaxShownLen = 16;
    std::fprintf(stderr, "usb %s ep%u len %zu rc %d:", dir, static_cast<unsigned>(ep), data.size(),
                 rc);
    for (size_t i = 0; i < std::min(data.size(), kMaxShownLen); ++i)
    {
        std::fprintf(stderr, " %02x", static_cast<unsigned>(data[i]));
    }
    std::fprintf(stderr, data.size() > kMaxShownLen ? " ...\n" : "\n");
}

uint8_t RangeCode(size_t vdivIndex)
{
    // the firmware skips code 4
    return static_cast<uint8_t>(vdivIndex > 3 ? vdivIndex + 1 : vdivIndex);
}

std::string Hex4(uint32_t v)
{
    std::array<char, 12> buf{};
    std::snprintf(buf.data(), buf.size(), "%04x", v);
    return buf.data();
}

}

std::string DriverHint(uint16_t pid)
{
#ifdef _WIN32
    return "The DSO-2100 (USB ID 0547:" + Hex4(pid) +
           ") is connected, but Windows has no WinUSB driver bound to it.\n\n"
           "Run Zadig (https://zadig.akeo.ie), choose Options > List All Devices, pick the "
           "device with USB ID 0547 " +
           Hex4(pid) +
           " and install WinUSB. The scope shows up "
           "under two IDs (1006 before the firmware is loaded, 1002 after), so this is needed "
           "once for each. See docs/DRIVERS.md.";
#else
    return "The DSO-2100 (USB ID 0547:" + Hex4(pid) +
           ") is connected but cannot be opened (permission denied).\n\n"
           "Install the udev rule and replug the scope:\n"
           "  sudo cp 60-dso2100.rules /etc/udev/rules.d/\n"
           "  sudo udevadm control --reload-rules && sudo udevadm trigger\n"
           "See docs/DRIVERS.md.";
#endif
}

Dso2100Usb::Dso2100Usb() = default;

Dso2100Usb::~Dso2100Usb()
{
    Close();
    if (m_ctx != nullptr)
    {
        libusb_exit(m_ctx);
    }
}

bool Dso2100Usb::Fail(std::string msg)
{
    m_error = std::move(msg);
    return false;
}

bool Dso2100Usb::Init()
{
    if (m_ctx != nullptr)
    {
        return true;
    }
    const int rc = libusb_init(&m_ctx);
    if (rc != 0)
    {
        m_ctx = nullptr;
        return Fail("libusb_init failed: " + UsbError(rc));
    }
    return true;
}

DeviceProbe Dso2100Usb::Probe()
{
    DeviceProbe p;
    if (m_ctx == nullptr)
    {
        return p;
    }
    libusb_device** list  = nullptr;
    const ssize_t   count = libusb_get_device_list(m_ctx, &list);
    for (ssize_t i = 0; i < count; ++i)
    {
        libusb_device_descriptor d{};
        if (libusb_get_device_descriptor(list[i], &d) != 0 || d.idVendor != Dso2100::kVendorId)
        {
            continue;
        }
        if (d.idProduct == Dso2100::kPidBootloader)
        {
            p.bootloaderPresent = true;
        }
        else if (d.idProduct == Dso2100::kPidRunning)
        {
            p.runningPresent = true;
            if (m_handle != nullptr)
            {
                continue; // ours already
            }
        }
        else
        {
            continue;
        }
        // Opening tells a missing driver (Windows) or missing permission (Linux) apart.
        libusb_device_handle* h  = nullptr;
        const int             rc = libusb_open(list[i], &h);
        if (rc == 0)
        {
            libusb_close(h);
        }
        else
        {
            p.openError    = UsbError(rc);
            p.openErrorPid = d.idProduct;
        }
    }
    if (count >= 0)
    {
        libusb_free_device_list(list, 1);
    }
    return p;
}

bool Dso2100Usb::UploadFirmware(std::string_view intelHex)
{
    std::vector<Core::HexSegment> segments;
    try
    {
        segments = Core::ParseIntelHex(intelHex);
    }
    catch (const std::runtime_error& e)
    {
        return Fail(std::string("Firmware image: ") + e.what());
    }

    libusb_device_handle* h =
        libusb_open_device_with_vid_pid(m_ctx, Dso2100::kVendorId, Dso2100::kPidBootloader);
    if (h == nullptr)
    {
        return Fail(DriverHint(Dso2100::kPidBootloader));
    }

    const auto poke = [h](uint32_t addr, const uint8_t* data, size_t len)
    {
        constexpr auto kType =
            static_cast<uint8_t>(static_cast<unsigned>(LIBUSB_ENDPOINT_OUT) |
                                 static_cast<unsigned>(LIBUSB_REQUEST_TYPE_VENDOR) |
                                 static_cast<unsigned>(LIBUSB_RECIPIENT_DEVICE));
        return libusb_control_transfer(h, kType, kReqFirmwareLoad, static_cast<uint16_t>(addr), 0,
                                       const_cast<uint8_t*>(data), static_cast<uint16_t>(len),
                                       kUsbTimeoutMs);
    };

    bool ok = true;
    int  rc = poke(kAn21CpucsAddr, &kCpuReset, 1);
    if (rc < 0)
    {
        ok = Fail("Firmware upload: CPU reset failed (" + UsbError(rc) + ")");
    }
    for (const Core::HexSegment& seg : segments)
    {
        for (size_t off = 0; ok && off < seg.data.size(); off += kUploadChunkLen)
        {
            const size_t len = std::min(kUploadChunkLen, seg.data.size() - off);
            rc = poke(seg.address + static_cast<uint32_t>(off), seg.data.data() + off, len);
            if (rc != static_cast<int>(len))
            {
                ok = Fail("Firmware upload: write at 0x" +
                          Hex4(seg.address + static_cast<uint32_t>(off)) + " failed (" +
                          (rc < 0 ? UsbError(rc) : std::string("short write")) + ")");
            }
        }
    }
    if (ok)
    {
        // The 8051 starts and the device drops off the bus to re-enumerate: errors expected.
        poke(kAn21CpucsAddr, &kCpuRun, 1);
    }
    libusb_close(h);
    return ok;
}

bool Dso2100Usb::Open()
{
    Close();
    libusb_device_handle* h =
        libusb_open_device_with_vid_pid(m_ctx, Dso2100::kVendorId, Dso2100::kPidRunning);
    if (h == nullptr)
    {
        return Fail("DSO-2100 (0547:1002) not found or not accessible");
    }
    libusb_set_auto_detach_kernel_driver(h, 1);

    // The original driver uses the vendor-specific interface (it has 14 endpoints).
    int                       iface = 0;
    libusb_config_descriptor* cfg   = nullptr;
    if (libusb_get_active_config_descriptor(libusb_get_device(h), &cfg) == 0)
    {
        for (int i = 0; i < cfg->bNumInterfaces; ++i)
        {
            const libusb_interface& it = cfg->interface[i];
            if (it.num_altsetting > 0 &&
                it.altsetting[0].bInterfaceClass == LIBUSB_CLASS_VENDOR_SPEC)
            {
                iface = it.altsetting[0].bInterfaceNumber;
                break;
            }
        }
        libusb_free_config_descriptor(cfg);
    }
    const int rc = libusb_claim_interface(h, iface);
    if (rc != 0)
    {
        libusb_close(h);
        return Fail("Cannot claim USB interface " + std::to_string(iface) + ": " + UsbError(rc));
    }
    m_handle    = h;
    m_interface = iface;
    m_lastGain.reset();

    if (!SendConfig(kInitConfig) || !SendCommand(kEpControl, kCmdIdle))
    {
        const std::string err = m_error;
        Close();
        return Fail(err);
    }
    return true;
}

void Dso2100Usb::Close()
{
    if (m_handle == nullptr)
    {
        return;
    }
    for (int i = 0; i < kUsbRetryCount; ++i)
    {
        uint8_t            cmd             = kCmdIdle;
        int                tr              = 0;
        constexpr unsigned kCloseTimeoutMs = 200;
        libusb_bulk_transfer(m_handle, kEpControl, &cmd, 1, &tr, kCloseTimeoutMs);
    }
    libusb_release_interface(m_handle, m_interface);
    libusb_close(m_handle);
    m_handle    = nullptr;
    m_interface = -1;
}

std::string Dso2100Usb::UsbSpeed() const
{
    if (m_handle == nullptr)
    {
        return "not open";
    }
    switch (libusb_get_device_speed(libusb_get_device(m_handle)))
    {
    case LIBUSB_SPEED_LOW:  return "low speed (1.5 Mbit/s)";
    case LIBUSB_SPEED_FULL: return "full speed (12 Mbit/s)";
    case LIBUSB_SPEED_HIGH: return "high speed (480 Mbit/s)";
    default:                return "unknown";
    }
}

bool Dso2100Usb::BulkWrite(uint8_t ep, std::span<const uint8_t> data)
{
    int tr = 0;
    int rc = LIBUSB_ERROR_TIMEOUT;
    for (int i = 0; i < kUsbRetryCount && rc == LIBUSB_ERROR_TIMEOUT; ++i)
    {
        rc = libusb_bulk_transfer(m_handle, ep, const_cast<uint8_t*>(data.data()),
                                  static_cast<int>(data.size()), &tr, kUsbTimeoutMs);
    }
    Trace("out", ep, data, rc);
    if (rc != 0)
    {
        return Fail("USB write to EP" + std::to_string(ep) + " failed: " + UsbError(rc));
    }
    return true;
}

bool Dso2100Usb::BulkRead(uint8_t ep, std::span<uint8_t> data, int& transferred)
{
    int rc = LIBUSB_ERROR_TIMEOUT;
    for (int i = 0; i < kUsbRetryCount && rc == LIBUSB_ERROR_TIMEOUT; ++i)
    {
        rc = libusb_bulk_transfer(m_handle, ep | LIBUSB_ENDPOINT_IN, data.data(),
                                  static_cast<int>(data.size()), &transferred, kUsbTimeoutMs);
    }
    Trace("in ", ep, data.first(static_cast<size_t>(std::max(transferred, 0))), rc);
    if (rc != 0)
    {
        return Fail("USB read from EP" + std::to_string(ep) + " failed: " + UsbError(rc));
    }
    return true;
}

bool Dso2100Usb::SendCommand(uint8_t ep, uint8_t cmd)
{
    return BulkWrite(ep, std::span<const uint8_t>(&cmd, 1));
}

bool Dso2100Usb::SendConfig(const std::array<uint8_t, 8>& cfg)
{
    return BulkWrite(kEpConfig, cfg);
}

AcquireResult Dso2100Usb::Acquire(const Core::AcqSettings& s, Core::Frame& out,
                                  const std::atomic<bool>& abort)
{
    if (m_handle == nullptr)
    {
        Fail("Device not open");
        return AcquireResult::kError;
    }
    const int  divider = Core::kRateDividers.at(s.rateIndex);
    const bool hwTrig  = s.trig.hardwareTrigger || s.trig.source == Core::TriggerSource::kExt;

    uint8_t gain =
        static_cast<uint8_t>((RangeCode(s.ch[0].vdivIndex) << 4U) | RangeCode(s.ch[1].vdivIndex));
    if (s.ch[0].coupling == Core::Coupling::kAc)
    {
        gain |= kCfgCh1Ac;
    }
    if (s.ch[1].coupling == Core::Coupling::kAc)
    {
        gain |= kCfgCh2Ac;
    }
    uint8_t trigSrc = kTrigSrcNone;
    if (hwTrig)
    {
        switch (s.trig.source)
        {
        case Core::TriggerSource::kCh1: trigSrc = kTrigSrcCh1; break;
        case Core::TriggerSource::kCh2: trigSrc = kTrigSrcCh2; break;
        case Core::TriggerSource::kExt: trigSrc = kTrigSrcExt; break;
        }
    }
    const auto             divRaw = static_cast<uint16_t>(divider - 1);
    std::array<uint8_t, 8> cfg    = {kCfgHeader,
                                     gain,
                                     static_cast<uint8_t>(divRaw & 0xffU),
                                     static_cast<uint8_t>(divRaw >> 8U),
                                     kCtrlIdle,
                                     0x00, // unknown; 0 in the original software
                                     trigSrc,
                                     kCfgTrailer};

    uint8_t ctrl =
        hwTrig ? (s.trig.edge == Core::TriggerEdge::kFalling ? kCtrlFalling : 0) : kCtrlFreeRun;
    if (divider > 1)
    {
        ctrl |= kCtrlDivided;
    }
    const double recordSec =
        static_cast<double>(Core::kRecordLen) * divider / Core::kBaseSampleRateHz;

    // The first record after a range or coupling change still holds samples taken with the old
    // setting (seen on the hardware), so it is taken free-running and dropped.
    const bool settle = m_lastGain != gain;
    m_lastGain        = gain;
    if (settle)
    {
        const AcquireResult r = CaptureRecord(cfg, kCtrlFreeRun | (ctrl & kCtrlDivided), false,
                                              s.trig.mode, recordSec, abort);
        if (r != AcquireResult::kOk)
        {
            m_lastGain.reset();
            return r;
        }
    }
    const AcquireResult r = CaptureRecord(cfg, ctrl, hwTrig, s.trig.mode, recordSec, abort);
    if (r != AcquireResult::kOk)
    {
        return r;
    }

    const auto ch1 = m_rx.begin() + kCh1Offset;
    const auto ch2 = m_rx.begin() + kCh2Offset;
    out.raw[0].assign(ch1, ch1 + Core::kRecordLen);
    out.raw[1].assign(ch2, ch2 + Core::kRecordLen);
    out.sampleRateHz = Core::kBaseSampleRateHz / divider;
    out.demo         = false;
    out.hwTriggered  = hwTrig;
    out.timing       = m_timing;
    return AcquireResult::kOk;
}

AcquireResult Dso2100Usb::CaptureRecord(std::array<uint8_t, 8> cfg, uint8_t ctrl, bool hwTrig,
                                        Core::TriggerMode mode, double recordSec,
                                        const std::atomic<bool>& abort)
{
    const auto armStart = Clock::now();
    if (!SendCommand(kEpCapture, kCmdCapture) || !SendConfig(cfg))
    {
        return AcquireResult::kError;
    }
    cfg[4] = ctrl;
    if (!SendConfig(cfg) || !SendCommand(kEpCapture, kCmdCapture))
    {
        return AcquireResult::kError;
    }

    const auto waitStart   = Clock::now();
    // Wait for a full buffer.
    const auto autoTimeout = std::chrono::duration_cast<Clock::duration>(
                                 std::chrono::duration<double>(recordSec * kAutoTimeoutRecords)) +
                             kAutoTimeoutExtra;
    const auto start       = Clock::now();
    for (;;)
    {
        if (abort.load())
        {
            return AcquireResult::kAborted;
        }
        uint8_t status = 0;
        int     got    = 0;
        if (!SendCommand(kEpStatus, kCmdStatus) ||
            !BulkRead(kEpStatus, std::span<uint8_t>(&status, 1), got))
        {
            return AcquireResult::kError;
        }
        if (got == 1 && status == kStatusFull)
        {
            break;
        }
        const auto waited = Clock::now() - start;
        if (hwTrig && mode == Core::TriggerMode::kAuto && waited > autoTimeout)
        {
            return AcquireResult::kTimeout;
        }
        if (!hwTrig && waited > autoTimeout + kFreeRunGrace)
        {
            Fail("The capture did not complete");
            return AcquireResult::kError;
        }
        if (recordSec > kSlowCaptureSec)
        {
            std::this_thread::sleep_for(kStatusPollPause);
        }
    }

    const auto readStart = Clock::now();
    cfg[4]               = kCtrlIdle;
    if (!SendConfig(cfg) || !SendCommand(kEpSamples, kCmdReadBuffer))
    {
        return AcquireResult::kError;
    }
    m_rx.resize(kReadLen);
    int got = 0;
    if (!BulkRead(kEpSamples, m_rx, got))
    {
        return AcquireResult::kError;
    }
    if (static_cast<size_t>(got) < kCh2Offset + Core::kRecordLen)
    {
        Fail("Short sample buffer (" + std::to_string(got) + " bytes)");
        return AcquireResult::kError;
    }
    const auto Ms = [](Clock::duration d)
    { return std::chrono::duration<double, std::milli>(d).count(); };
    m_timing.armMs  = Ms(waitStart - armStart);
    m_timing.waitMs = Ms(readStart - waitStart);
    m_timing.readMs = Ms(Clock::now() - readStart);
    return AcquireResult::kOk;
}

}
