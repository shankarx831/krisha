#!/bin/bash
# postinstall.sh for KRISHA
# Set owner and permissions for the HAL driver, then restart coreaudiod.

set -e

DRIVER_PATH="/Library/Audio/Plug-Ins/HAL/KrishaDriver.driver"

if [ -d "$DRIVER_PATH" ]; then
    echo "Configuring permissions for KrishaDriver.driver..."
    chown -R root:wheel "$DRIVER_PATH"
    chmod -R 755 "$DRIVER_PATH"
fi

echo "Kickstarting coreaudiod to load the KRISHA driver..."
launchctl kickstart -kp system/com.apple.audio.coreaudiod

exit 0
