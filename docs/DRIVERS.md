# USB driver setup

The DSO-2100 has an EZ-USB chip without firmware in flash. Every time it is plugged in it
shows up as a **blank device**, gets its firmware uploaded, and then **re-appears under a second
USB ID**:

| Stage | USB ID | Shows up as |
|---|---|---|
| 1 | `0547:1006` | Anchor Chips "Hantek DSO-2100 UF" (bootloader) |
| 2 | `0547:1002` | the running oscilloscope |

The original software used two Windows kernel drivers for this (`dso21001.sys` for stage 1,
`dso21002.sys` for stage 2). Those are 32-bit drivers from 2005 and do not load on 64-bit
Windows 10/11. DSO-2100 Scope does both stages itself through libusb, so it only needs the
generic **WinUSB** driver (Windows) or access rights (Linux) for **both IDs**.

## Windows 10 / 11

One-time setup, with [Zadig](https://zadig.akeo.ie):

1. Plug in the scope. Device Manager shows an unknown device (or "DSO-2100 USB driver1").
2. Start Zadig, choose **Options > List All Devices**.
3. Select the device with USB ID **0547 1006**, choose **WinUSB** as the driver, press
   **Install Driver** (or Replace Driver).
4. Start DSO-2100 Scope. It uploads the firmware; the scope disconnects and comes back as
   **0547 1002**. The app tells you that this ID has no driver yet.
5. In Zadig (refresh the list), select **0547 1002**, install **WinUSB** again.
6. Restart the app (or wait: it retries every second). The status shows "DSO-2100 connected".

After this Windows remembers both drivers; next time just plug in and start the app.

If the original drivers were installed before, Zadig replaces them. To go back, uninstall the
device in Device Manager with "Delete the driver software for this device".

## Linux

```
sudo cp 60-dso2100.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Replug the scope. The rule gives the logged-in user access to both IDs. `fxload` and
`load-fw-and-run-app.sh` are no longer needed (they still work: the app then finds the scope
already in stage 2).

## Devcontainer (WSL2) with the real scope

The Windows build runs natively and needs only the steps above. To run the *Linux* build from
the devcontainer against the scope, attach the device to WSL with
[usbipd-win](https://github.com/dorssel/usbipd-win), once per ID (the ID changes after the
firmware upload, so attach again after stage 1):

```
usbipd list
usbipd bind --busid <busid>
usbipd attach --wsl --busid <busid>
```

## Troubleshooting

| Message / symptom | Cause |
|---|---|
| "no WinUSB driver bound to it" (0547:1006) | step 3 not done |
| "no WinUSB driver bound to it" (0547:1002) | step 5 not done |
| "permission denied" on Linux | udev rule missing, or not replugged |
| "did not re-enumerate as 0547:1002" | firmware upload failed or USB hub problem: replug, try another port |
| Traces upside down | use **Invert** on the channel (raw value direction not yet verified on hardware) |
| Zero line off | Utility > Calibrate zero, with the inputs disconnected |
