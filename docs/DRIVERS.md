# USB driver setup

**Short version:** on Windows install **WinUSB** with Zadig, **twice** (once for each of the
scope's two USB IDs). It is a one-time setup per PC. On Linux install one udev rule. You do
**not** need the original drivers or `fxload`.

## Why two IDs

The DSO-2100 has an EZ-USB chip with no firmware in flash. Every time it gets power it shows up
as a **blank device**, the PC uploads the firmware into its RAM, and it then **re-appears under a
second USB ID**:

| Stage | USB ID | Windows shows it as | What happens |
|---|---|---|---|
| 1 | `0547:1006` | unknown device (or "DSO-2100 USB driver1") | the app uploads the firmware (about 1 s) |
| 2 | `0547:1002` | **"EZ-USB Device"** | the running oscilloscope; the app reads samples from it |

The original software used two Windows kernel drivers for this (`dso21001.sys` for stage 1,
`dso21002.sys` for stage 2). They are 32-bit drivers from 2005 and do not load on 64-bit
Windows 10/11. DSO-2100 Scope does both stages itself through libusb, so it only needs the
generic Microsoft **WinUSB** driver on **both IDs**.

## Windows 10 / 11

You need: the scope, a USB cable, [Zadig](https://zadig.akeo.ie) (a single .exe, no install) and
DSO-2100 Scope (`dso2100scope.exe` from the Windows zip).

### Step 1: driver for the blank scope (0547:1006)

1. Plug in the scope. Windows finds no driver for it; that is expected.
2. Start **Zadig** (allow the administrator prompt).
3. Menu **Options > List All Devices** (otherwise Zadig hides devices that already have a driver).
4. In the drop-down pick the scope. Check the **USB ID** fields: they must read **`0547`** and
   **`1006`**. (The name may be empty or "DSO-2100 USB driver1".)
5. Left of the green arrow it shows the current driver (often `(NONE)`); right of it, choose
   **WinUSB**.
6. Press **Install Driver** (or **Replace Driver**). Wait for "The driver was installed
   successfully" (can take a minute).

### Step 2: start the app, it uploads the firmware

7. Start `dso2100scope.exe`. The status at the top right shows **"Loading firmware..."**.
8. The scope's **blue LED starts blinking**: the firmware runs. The scope drops off USB and comes
   back as `0547:1002`, which has no driver yet, so the app shows a **"USB driver needed"**
   message naming `0547:1002`. That is expected: leave the app running.

### Step 3: driver for the running scope (0547:1002)

9. In Zadig: **Options > List All Devices** again (or press the refresh, or restart Zadig).
10. Pick **"EZ-USB Device"**. Check the USB ID: **`0547`** and **`1002`**.
11. Choose **WinUSB**, press **Install Driver**, wait for success.
12. Back in the app: within a few seconds the status turns green, **"DSO-2100 connected"**
    (restart the app if it does not). With nothing on the inputs you see two flat lines at 0 V.

### Done: what happens from now on

- Windows remembers both drivers. Next time: plug in, start the app; it uploads the firmware by
  itself every time the scope is powered up ("Loading firmware..." -> "DSO-2100 connected").
- Any USB port works: the driver is matched by USB ID, not by port. Reboots and Windows updates
  keep it.
- Repeat the setup only on **another PC**, after **reinstalling Windows**, after uninstalling the
  device with "Delete the driver software", or if the **original vendor software** is installed
  again (its drivers take the IDs back).

### Check the drivers from PowerShell

```powershell
Get-PnpDevice | Where-Object InstanceId -like '*VID_0547*' | ForEach-Object { "{0,-8} {1,-7} {2}" -f $_.Status, (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_Service).Data, $_.InstanceId }
```

Both lines must show `WinUSB` (the ID that is not plugged in at the moment shows `Unknown`). An
empty driver column with status `Error` (problem code 28) means that ID still needs step 1 or 3.

### Going back to the original software

Device Manager, the scope, **Uninstall device** with **"Delete the driver software for this
device"** ticked, for both IDs; then install the original software from `original-software/`.
Note that its drivers only work on 32-bit Windows.

## Linux

```bash
sudo cp 60-dso2100.rules /etc/udev/rules.d/
```

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Replug the scope. The rule gives the logged-in user access to both IDs; no driver install is
needed (the app detaches any kernel driver itself). `fxload` and `load-fw-and-run-app.sh` are no
longer needed (they still work: the app then finds the scope already in stage 2).

## Devcontainer (WSL2) with the real scope

The Windows build runs natively and needs only the Windows steps above. To run the *Linux* build
from the devcontainer against the scope, attach the device to WSL with
[usbipd-win](https://github.com/dorssel/usbipd-win), once per ID (the ID changes after the
firmware upload, so attach again after stage 1):

```
usbipd list
usbipd bind --busid <busid>
usbipd attach --wsl --busid <busid>
```

## Troubleshooting

| Message / symptom | Cause / fix |
|---|---|
| "USB driver needed ... 0547:1006" | step 1 not done |
| "USB driver needed ... 0547:1002" | step 3 not done (normal right after the first firmware upload) |
| Zadig does not list the scope | Options > List All Devices; replug; try another USB port or cable |
| Stays at "Demo signals (no scope)" | check the drivers from PowerShell (above); restart the app |
| "did not re-enumerate as 0547:1002" | firmware upload failed or USB hub problem: replug, try a port directly on the PC |
| "permission denied" on Linux | udev rule missing, or the scope was not replugged |
| Traces upside down | use **Invert** on the channel (raw value direction not yet verified on hardware) |
| Zero line off | Utility > Calibrate zero, with the inputs disconnected |
| Anything else | start the app with `DSO_USB_TRACE=1` and keep the output (README, Debugging) |
