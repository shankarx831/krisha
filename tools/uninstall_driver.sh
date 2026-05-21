#!/bin/bash
# Uninstall Krisha driver (for testing)

echo "  Uninstalling Krisha driver..."

osascript -e 'do shell script "rm -rf /Library/Audio/Plug-Ins/HAL/KrishaDriver.driver && killall coreaudiod" with administrator privileges'

if [ $? -eq 0 ]; then
    echo "OK: Driver uninstalled successfully"
    echo "OK: Audio system restarted"
else
    echo "ERROR: Failed to uninstall driver"
    exit 1
fi

echo ""
echo "Driver removed. Audio system will restart momentarily."
