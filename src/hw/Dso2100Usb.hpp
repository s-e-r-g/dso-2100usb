#pragma once

// DSO-2100 over libusb. Replaces both original Windows drivers: stage 1 (dso21001.sys, or
// fxload on Linux) uploads the firmware to the blank EZ-USB at 0547:1006; the device then
// re-enumerates as 0547:1002 and stage 2 (dso21002.sys) talks to it. Acquisition thread only.

#include "hw/ScopeBackend.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct libusb_context;
struct libusb_device_handle;

namespace Hw
{

namespace Dso2100
{
constexpr uint16_t kVendorId      = 0x0547; // Anchor Chips (EZ-USB AN21xx)
constexpr uint16_t kPidBootloader = 0x1006; // blank EZ-USB, waiting for firmware
constexpr uint16_t kPidRunning    = 0x1002; // running the scope firmware
}

// What a scan of the bus found.
struct DeviceProbe
{
    bool        bootloaderPresent = false;
    bool        runningPresent    = false;
    // Present but cannot be opened: on Windows WinUSB is not bound to that ID, on Linux the
    // udev rule is missing. openErrorPid says which of the two IDs.
    std::string openError;
    uint16_t    openErrorPid = 0;
};

class Dso2100Usb final : public ScopeBackend
{
public:
    Dso2100Usb();
    ~Dso2100Usb();
    Dso2100Usb(const Dso2100Usb&)            = delete;
    Dso2100Usb& operator=(const Dso2100Usb&) = delete;

    bool        Init();  // false: libusb unusable, see LastError()
    DeviceProbe Probe(); // scans the bus; does not change the open device

    // Stage 1: uploads an Intel HEX image to a device in bootloader mode. true: the device
    // was started and is now re-enumerating (takes about 1-2 s).
    bool UploadFirmware(std::string_view intelHex);

    // Stage 2: opens and initialises the device running the firmware.
    bool Open();
    void Close();
    bool IsOpen() const { return m_handle != nullptr; }

    AcquireResult Acquire(const Core::AcqSettings& s, Core::Frame& out,
                          const std::atomic<bool>& abort) override;

    // The whole sample buffer of the last capture, as read from EP2 (for diagnostics).
    std::span<const uint8_t> LastRawBuffer() const { return m_rx; }

private:
    bool          BulkWrite(uint8_t ep, std::span<const uint8_t> data);
    bool          BulkRead(uint8_t ep, std::span<uint8_t> data, int& transferred);
    bool          SendCommand(uint8_t ep, uint8_t cmd);
    bool          SendConfig(const std::array<uint8_t, 8>& cfg);
    bool          Fail(std::string msg);
    // One capture with the given config and arming byte; the samples end up in m_rx.
    AcquireResult CaptureRecord(std::array<uint8_t, 8> cfg, uint8_t ctrl, bool hwTrig,
                                Core::TriggerMode mode, double recordSec,
                                const std::atomic<bool>& abort);

    libusb_context*        m_ctx       = nullptr;
    libusb_device_handle*  m_handle    = nullptr;
    int                    m_interface = -1;
    std::vector<uint8_t>   m_rx;
    std::optional<uint8_t> m_lastGain; // gain byte of the last capture; none after Open()
};

// What the user has to do when the device with `pid` is present but cannot be opened.
std::string DriverHint(uint16_t pid);

}
