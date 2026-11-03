# dso-2100usb

**DSO-2100 Scope**: modern oscilloscope software for the old Hantek / Voltcraft **DSO-2100 USB**
(2 channels, 8 bit, 50 MS/s), for Windows 10/11 and Linux. Qt 6, libusb, a bench-scope style UI
(Rigol / R&S feel): run/stop/single, autoset, software and hardware trigger, averaging,
persistence, XY, math and FFT, 16 automatic measurements with statistics, cursors, reference
waveforms, CSV/PNG export, zero calibration.

The app loads the scope's firmware itself, so the original 32-bit Windows drivers and `fxload`
are no longer needed. You only bind WinUSB once on Windows, or install a udev rule on Linux:
**[docs/DRIVERS.md](docs/DRIVERS.md)**.

### Quick start on Windows

1. Unzip `dso2100scope-windows.zip` (from CI, or `make windows`).
2. Plug in the scope; with [Zadig](https://zadig.akeo.ie) (Options > List All Devices) install
   **WinUSB** for USB ID **0547 1006**.
3. Start `dso2100scope.exe`: it uploads the firmware and the scope comes back as **0547 1002**.
4. In Zadig install **WinUSB** for **0547 1002** ("EZ-USB Device"). The app shows
   "DSO-2100 connected".

Steps 2 and 4 are needed once per PC. Step-by-step details, what to expect and
troubleshooting: [docs/DRIVERS.md](docs/DRIVERS.md).

Requirements and status: [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).
Coding style: [CODING_STYLE.md](CODING_STYLE.md).

## Use

Without a scope the app runs on simulated signals (Utility > Demo signals).

| Action | How |
|---|---|
| Time/div | mouse wheel, pinch, or Horizontal > Scale |
| V/div | Ctrl + wheel (last used channel), or the channel panel |
| Channel position | drag the coloured marker on the left edge |
| Trigger level | drag the orange marker on the right edge, or 50 % |
| Move in time | drag the screen; double-click resets the delay |
| Run/Stop, Single, Autoset, Clear | Space, S, A, C |

## Develop

Everything builds in the devcontainer (`.devcontainer/`, Ubuntu 24.04 + Qt 6.8 for Linux *and*
Windows + MinGW-w64), also when your PC runs Windows: the container builds the Linux program
natively and **cross-compiles the Windows `.exe`**. Open the folder in VS Code and choose
"Reopen in Container" (Docker Desktop with the WSL2 backend), then:

```bash
make help
```

```bash
make && make test && make windows
```

`make check` runs everything CI runs. The Makefile only names steps; the build itself is CMake
(`CMakePresets.json`: `linux`, `linux-debug`, `windows`, `windows-native`).

Third-party versions (Qt, libusb, aqtinstall, clang-format, clang-tidy) are pinned as `ARG`s in
`.devcontainer/Dockerfile`. `make check-thirdparties` shows newer upstream versions,
`make upgrade-thirdparties [LIB=qt]` writes them; then rebuild the container and `make check`.

The Windows build lands in `build/windows/dist/dso2100scope-windows.zip` (exe + Qt DLLs); the
folder `build/windows/dist/dso2100scope/` can be run straight from Windows Explorer, with the
scope on the Windows USB port.

Before committing: `cmake --build --preset linux --target format` (CI checks it).
CI (`.github/workflows/build.yml`) runs the same steps for both platforms and publishes both builds.

### Debugging

- **In the container** (default): F5 with "Debug (devcontainer, Linux)" builds the
  `linux-debug` preset and starts it under gdb. The window shows on the Windows desktop through
  WSLg. Demo signals work right away; for the real scope attach it to WSL with `usbipd`
  (docs/DRIVERS.md).
- **Native Windows**, for stepping through code with the scope on Windows USB: install Qt 6.8
  with the MinGW 13 kit (Qt online installer, includes gdb, CMake and Ninja) and the
  [libusb release](https://github.com/libusb/libusb/releases). Open the folder in VS Code
  *without* the container, put Qt's `mingw_64/bin` and `Tools/mingw1310_64/bin` on `PATH`,
  configure with `-DCMAKE_PREFIX_PATH=<Qt>/6.8.3/mingw_64 -DLIBUSB_ROOT=<libusb>` and use
  "Debug (Windows native)".
- **USB protocol trace**: set `DSO_USB_TRACE=1` to print every USB transfer to stderr.

## Layout

| Path | What |
|---|---|
| `src/core/` | portable logic: hardware model, trigger, measurements, FFT, Intel HEX |
| `src/hw/` | libusb backend (firmware upload + protocol) and the simulator |
| `src/app/` | acquisition thread, frame processing |
| `src/gui/` | main window and the scope screen |
| `tests/core/` | GoogleTest unit tests |
| `drivers/` | original firmware (`dso2100-firmware.hex`, embedded in the app) and drivers |
| `original-software/` | the vendor's Windows software, for reference |
| `60-dso2100.rules` | udev rule for Linux |
| `load-fw-and-run-app.sh` | old Linux helper using fxload (not needed any more) |

## Credits and other software

The USB protocol comes from [oscope2100](https://github.com/guntars-lemps/oscope2100) (GTK, Linux).
See also [OpenHantek](https://github.com/OpenHantek/openhantek) and
[sigrok](https://sigrok.org/wiki/Main_Page).
