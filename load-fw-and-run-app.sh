#!/bin/bash

# --- Configuration ---
FIRMWARE_PATH="drivers/dso2100-firmware.hex"
FXLOAD_BIN="/sbin/fxload"
APP_BIN="/usr/local/bin/oscope2100"  # Adjust path if needed

# --- Vendor/Product IDs ---
# E.g. "Bus 001 Device 016: ID 0547:1006 Anchor Chips, Inc. Hantek DSO-2100 UF"
BOOTLOADER_VID="0547"
BOOTLOADER_PID="1006"
# E.g. "Bus 001 Device 017: ID 0547:1002 Anchor Chips, Inc. Python2 WDM Encoder"
ACTIVE_VID="0547"
ACTIVE_PID="1002"

# --- Step 0: Already active? ---
echo "Checking if device is already in active (firmware) mode..."
if lsusb | grep -q "$ACTIVE_VID:$ACTIVE_PID"; then
    echo "Device is already active. Skipping firmware upload."
else
    echo "Device not yet active. Checking bootloader mode..."
    BOOT_DEV=$(lsusb | grep "$BOOTLOADER_VID:$BOOTLOADER_PID")

    if [ -n "$BOOT_DEV" ]; then
        echo "Bootloader device found. Uploading firmware..."

        DEV_BUS=$(echo "$BOOT_DEV" | awk '{print $2}')
        DEV_ADDR=$(echo "$BOOT_DEV" | awk '{print $4}' | sed 's/://')
        DEV_PATH="/dev/bus/usb/$DEV_BUS/$DEV_ADDR"

        sudo "$FXLOAD_BIN" -t an21 -I "$FIRMWARE_PATH" -D "$DEV_PATH"
        if [ $? -ne 0 ]; then
            echo "Firmware upload failed."
            exit 1
        fi

        echo "Firmware uploaded. Waiting for re-enumeration..."
        sleep 2
    else
        echo "No bootloader or active device detected. Is the device connected?"
        exit 1
    fi
fi

# --- Final check for active mode ---
echo "Verifying device is now in active mode..."
for i in {1..10}; do
    if lsusb | grep -q "$ACTIVE_VID:$ACTIVE_PID"; then
        echo "Device is ready."
        break
    fi
    echo "Waiting for device... ($i)"
    sleep 1
done

if ! lsusb | grep -q "$ACTIVE_VID:$ACTIVE_PID"; then
    echo "Device not found in active mode after firmware upload."
    exit 1
fi

# --- Step 3: Launch Application ---
echo "Launching oscilloscope application..."
exec "$APP_BIN"
