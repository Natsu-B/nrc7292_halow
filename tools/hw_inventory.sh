#!/usr/bin/env bash
set -u
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/logs"
{
  for cmd in 'date --iso-8601=seconds' 'uname -a' 'nixos-version' 'pwd' 'lsusb' 'lsusb -t' 'ls -l /dev/serial/by-id/' 'ls -l /dev/ttyACM* /dev/ttyUSB*' 'ip -br link' 'ip -br addr' 'ip route' 'ip neigh' 'nmcli device' 'nmcli connection show' 'id'; do
    printf '\n$ %s\n' "$cmd"
    bash -c "$cmd" 2>&1 || true
  done
  for dev in /dev/serial/by-id/*; do
    test -e "$dev" || continue
    printf '\n$ udevadm info -q property -n %s\n' "$dev"
    udevadm info -q property -n "$dev"
    test -r "$dev" && test -w "$dev" && printf 'SERIAL_RW=YES %s\n' "$dev"
  done
} > "$root/logs/hardware_inventory.txt"
printf '%s\n' "$root/logs/hardware_inventory.txt"
