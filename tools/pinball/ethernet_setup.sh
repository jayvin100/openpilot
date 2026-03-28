#!/bin/bash
set -e

COMMA_IP="192.168.50.1"
WORK_IP="192.168.50.2"
CON="pinball"

if [ -f /TICI ]; then
  # comma side
  nmcli con delete $CON 2>/dev/null || true
  nmcli con add con-name $CON type ethernet ifname eth0 ipv4.addresses $COMMA_IP/24 ipv4.method manual
  nmcli con up $CON
  echo "comma: $COMMA_IP on eth0"
else
  # workstation side
  if [[ "$(uname)" == "Darwin" ]]; then
    # macOS: find USB ethernet service name
    SVC=$(networksetup -listallhardwareports | grep -A1 'USB' | grep 'Device' | awk '{print $2}' | head -1)
    [ -z "$SVC" ] && { echo "no USB ethernet found"; exit 1; }
    sudo ifconfig "$SVC" $WORK_IP/24 up
    echo "workstation: $WORK_IP on $SVC"
  else
    # Linux: find USB ethernet dongle (enx*)
    IFACE=$(ip -br link | grep -oP '^enx\S+' | head -1)
    [ -z "$IFACE" ] && { echo "no USB ethernet found"; exit 1; }
    nmcli con delete $CON 2>/dev/null || true
    nmcli con add con-name $CON type ethernet ifname "$IFACE" ipv4.addresses $WORK_IP/24 ipv4.method manual
    nmcli con up $CON
    echo "workstation: $WORK_IP on $IFACE"
  fi
fi
