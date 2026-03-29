#!/usr/bin/env bash
# Setup USB networking between host and comma device
# Device: 192.168.55.1 (usb0)
# Host:   192.168.55.100 (auto-detected Linux USB Gadget interface)

DEVICE_IP="192.168.55.1"
HOST_IP="192.168.55.100"
SUBNET="255.255.255.0"

# --- Find the host USB gadget interface ---
HOST_IF=$(networksetup -listallhardwareports 2>/dev/null | awk '/Linux USB Gadget/{getline; print $2}')
if [ -z "$HOST_IF" ]; then
  echo "ERROR: No 'Linux USB Gadget' interface found. Is the comma device plugged in via USB?"
  exit 1
fi
echo "Host interface: $HOST_IF"

# --- Device side: bring up usb0 ---
echo "Configuring device usb0..."
adb shell "sudo ip link set usb0 up 2>/dev/null; sudo ip addr add ${DEVICE_IP}/24 dev usb0 2>/dev/null; true"

# --- Host side: assign IP (idempotent) ---
echo "Configuring $HOST_IF on host..."
sudo ifconfig "$HOST_IF" "$HOST_IP" netmask "$SUBNET" up 2>/dev/null || true

# --- Verify ---
for i in 1 2 3; do
  if ping -c 1 -W 1 "$DEVICE_IP" > /dev/null 2>&1; then
    echo "OK: device reachable at $DEVICE_IP"
    echo "Bodyteleop UI: https://$DEVICE_IP:5000"
    exit 0
  fi
  sleep 1
done
echo "WARN: ping to $DEVICE_IP failed after 3 attempts"
exit 1
