# Requirements

What the DSO-2100 Scope software must do, as agreed with the owner (2026-11-03).
Status: [x] done, [ ] open.

## Goal

Normal, modern oscilloscope software for the old Hantek / Voltcraft **DSO-2100 USB** (2 channels,
8 bit, 50 MS/s), replacing the original Windows program. A UI with the feel and functions of a
Rigol or Rohde & Schwarz bench scope.

## Platforms

- [x] Windows 10/11 x64 and Linux, one code base, Qt 6 Widgets, C++20, CMake.
- [x] Built in a devcontainer on Windows: Linux build natively, Windows `.exe` cross-compiled
      (MinGW-w64 + Qt for Windows), packaged as a zip with the needed DLLs.
- [x] CI (GitHub Actions) builds both platforms in the same devcontainer, runs the unit tests
      and the format check, and publishes both builds as artifacts.
- [ ] Old tablet as the UI (later). Keep `core/` and `hw/` free of Qt; the scope view already
      supports touch (pinch = time/div, drag markers). Options depend on the tablet:
      Windows tablet (same app, touch layout), Android (Qt for Android, USB host via
      `UsbManager` fd -> `libusb_wrap_sys_device`), iPad (no USB: network bridge).

## USB and drivers ("the two-step driver trick")

The scope enumerates twice. The original software used two kernel drivers for this:

| Stage | USB ID | Original Windows driver | Linux original | This software |
|---|---|---|---|---|
| 1 bootloader | 0547:1006 (blank EZ-USB AN2131) | dso21001.sys uploads the firmware | fxload + udev | uploads `drivers/dso2100-firmware.hex` itself over libusb (EZ-USB 0xA0 request, CPUCS 0x7F92) |
| 2 running | 0547:1002 | dso21002.sys | oscope2100 via libusb | libusb bulk protocol |

- [x] Firmware upload built into the app (embedded resource); no fxload, no old .sys drivers.
- [x] Windows: WinUSB bound to **both** IDs (Zadig, or `pnputil` with an INF). The app detects
      "present but no driver" for each ID and tells the user exactly what to do.
- [x] Linux: udev rule `60-dso2100.rules` gives the logged-in user access to both IDs; kernel
      drivers are detached automatically. The app says when the rule is missing.
- [x] Hot plug: unplug/replug is detected; demo signals when no scope is connected (can be off).

## Scope functions

- [x] Run / Stop / Single, Autoset, Default setup, trigger status (TRIGGERED / AUTO / WAITING / STOPPED).
- [x] Channels: on/off, V/div 50 mV..5 V (x probe), DC/AC/GND, probe x1/x10/x100, invert,
      position.
- [x] Timebase 50 ns..1 s/div with automatic sample rate, horizontal delay (pan).
- [x] Trigger: Auto / Normal / Single, CH1 / CH2 / EXT, rising / falling, level by spin box or
      dragging the marker, 50 % button. Software trigger (any level, interpolated) with optional
      hardware trigger (always used for EXT).
- [x] Acquire: normal, averaging 2..128.
- [x] Display: vectors / dots, persistence (short, long, infinite), XY mode.
- [x] Numbers on the grid: time from the trigger, CH1 volts on the left, CH2 on the right.
- [x] Math: A+B, A-B, AxB, FFT (rectangle, Hann, Hamming, Blackman, flat top) in a split pane.
- [x] Measurements with statistics (mean/min/max): frequency, period, Vpp, Vmax, Vmin, Vamp,
      Vtop, Vbase, Vavg, Vrms, +/- width, duty, rise, fall, overshoot; per CH1/CH2/Math.
- [x] Cursors: time, voltage, both; readout of delta and 1/delta.
- [x] Reference waveforms R1/R2.
- [x] I2C decoding: bytes on a bus band on screen, a log, and a trigger on START.
- [x] Zero calibration per channel and range, saved.
- [x] Export: screenshot PNG, waveform CSV (whole record, time from trigger).
- [x] All settings saved between sessions.
- [x] Developer workflow like KM-Sweetch: `make` targets over CMake presets, pinned versions
      checked/upgraded by `make check-thirdparties` / `upgrade-thirdparties`, debugging with gdb
      in the devcontainer (WSLg) or natively on Windows, `DSO_USB_TRACE=1` USB log.
- [x] Usability: short choices as one-click segmented buttons, display tools in the top bar,
      a panel that fits without scrolling, a short hint on every control, touch-sized controls.
- [x] Zoom and pan inside a stopped capture.
- [x] Record overview bar: the whole capture with the on-screen window, drag to move in time.
- [ ] Measurements on a stopped capture and between the cursors (measurement gate).
- [ ] Windows driver install from inside the app (libwdi, like Zadig, one UAC prompt) instead of
      the manual Zadig steps.
- [ ] Verified on the real hardware (edge polarity of the hardware trigger, raw value
      direction, buffer offsets).

## Engineering

- [x] Coding style as in the KM-Sweetch project (CODING_STYLE.md), adapted to desktop:
      heap, exceptions, RTTI and the standard library are allowed; exceptions never cross
      into the Qt event loop.
- [x] clang-format / clang-tidy pinned in the devcontainer; unit tests (GoogleTest) for `core/`.
