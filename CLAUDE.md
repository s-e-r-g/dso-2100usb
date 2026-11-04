# DSO-2100 Scope

Qt 6 desktop oscilloscope software (Windows + Linux) for the Hantek/Voltcraft DSO-2100 USB.
Requirements and their status: [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md) (keep it updated
when the owner adds or changes a requirement). Style: [CODING_STYLE.md](CODING_STYLE.md), same
rules as the owner's KM-Sweetch project, adapted to desktop (heap, exceptions, RTTI allowed).

## Layout

| Folder | Namespace | Depends on | What |
|---|---|---|---|
| `src/core/` | `Core` | std only | hardware model (`Scope.hpp`), trigger, measurements, FFT, Intel HEX, formatting |
| `src/hw/` | `Hw` | `core/`, libusb | `Dso2100Usb` (firmware upload + protocol), `DemoBackend` |
| `src/app/` | `App` | `core/`, `hw/`, QtCore | `Acquisition` thread, `Processor` (frame -> screen) |
| `src/gui/` | `Gui` | everything, QtWidgets | `MainWindow`, `ScopeView` |
| `tests/core/` | | GoogleTest | unit tests for `core/` |
| `drivers/` | | | original firmware hex (embedded as a Qt resource) and .sys/.inf, never edited |
| `original-software/` | | | vendor installer, reference only, never edited |

## Build (devcontainer; on Windows the container is Linux and cross-compiles the .exe)

`make help` lists everything. `make` (Linux), `make test`, `make windows` (zip in
build/windows/dist), `make run`, `make screenshot` (headless smoke test), `make format`,
`make tidy`, `make check` (all CI does), `make check-thirdparties` / `upgrade-thirdparties`
(pinned versions are the ARGs in .devcontainer/Dockerfile, tools/thirdparty.sh).

CI (`.github/workflows/build.yml`) runs exactly this in the devcontainer for both platforms.
Debugging: `linux-debug` preset + gdb (VS Code F5, window via WSLg), or `windows-native` preset
on the Windows host with Qt's MinGW gdb (`.vscode/launch.json`). `DSO_USB_TRACE=1` logs USB.
Without a display, run the Linux build with `QT_QPA_PLATFORM=offscreen` (demo mode works).

## Hardware facts (do not lose)

- Two USB IDs: 0547:1006 = blank EZ-USB AN2131 (bootloader), 0547:1002 = running firmware.
  The app uploads `drivers/dso2100-firmware.hex` itself (vendor request 0xA0, CPUCS 0x7F92);
  it fits in internal RAM (< 0x2000), so `dso2100-loader.hex` is not needed.
- Windows needs WinUSB on both IDs (Zadig); Linux needs `60-dso2100.rules`. See docs/DRIVERS.md.
- Protocol (from github.com/guntars-lemps/oscope2100, documented in `src/hw/Dso2100Usb.cpp`):
  config on EP4, capture EP3, status EP7, samples EP2 (0xff80 bytes, CH1 at +2, CH2 at +0x8002),
  50 MHz / divider, 7 ranges 50 mV..5 V/div (code skips 4), 256 counts = 8 div, zero ~0x7f.
- Measured on hardware: connects at USB full speed (12 Mbit/s; the AN2131 has no high-speed
  PHY). Reading the 64 KB buffer takes ~57 ms, so ~18 frames/s is the ceiling; slow timebases add
  the record time (30000 samples / sample rate). App stages are < 1 ms plus paint (~10-30 ms, GUI
  thread). `DSO_PROFILE=<file>` logs the stages per second; `dso2100diag` shows USB speed and timing.
- Board (photos): Cypress AN2131QC, 50.000 MHz oscillator (Hosonic HO-15B), one National ADC1175-50
  (8 bit, 50 MS/s max) and one 32 KB IS62LV256L-15T SRAM per channel, a QFP logic chip with its
  marking removed. So 50 MS/s per channel is right; the "100MS/s" on the case is 2 x 50 MS/s
  (or would need interleaving both ADCs on one channel, which no known command does).
- The first record after a gain byte change holds old-range data: the driver drops it.
- Reading less than the whole 0xff80 buffer does not work: reads get 2x faster, but the firmware
  then takes no new capture and repeats the old record (identical data). Do not retry this.
- No hardware offset and no known trigger-level byte (config byte 5 is always 0): position is
  display-only, trigger level is applied in software. Not yet verified on hardware: edge bit
  polarity, raw value direction (use Invert if upside down).

## Working rules

- Conventional Commits (`feat(hw): ...`), code and docs in separate commits when practical.
- Run `format` before committing; CI fails on unformatted code and on warnings (`DSO_WERROR`).
- Exceptions must not escape into Qt slots/event handlers or the acquisition thread loop.
- `core/` and `hw/` stay Qt-free (a tablet/Android UI may reuse them later).
