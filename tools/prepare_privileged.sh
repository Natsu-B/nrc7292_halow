#!/usr/bin/env bash
# Temporary Stage-1 firewall rules and a bounded wired packet capture.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
TAG=codex-stage1-20260908
IFACE=enp0s31f6
TCPDUMP=/nix/store/c2paxp2v2s8siiwg7x4jh43a78yg7lhq-tcpdump-4.99.5/bin/tcpdump
if (( EUID != 0 )); then
  [[ -x "$TCPDUMP" ]] || { echo 'Missing prepared tcpdump store path; ask Codex to prepare it.' >&2; exit 2; }
  exec sudo -- bash "$0" "${1:-setup}"
fi
: "${SUDO_USER:?Run this script from your normal user account}"
: "${SUDO_UID:?Missing sudo user identity}"
[[ ${1:-setup} == setup || ${1:-setup} == cleanup ]] || { echo 'Usage: prepare_privileged.sh [setup|cleanup]' >&2; exit 2; }
ip link show dev "$IFACE" >/dev/null
rule_udp=(-i "$IFACE" -p udp --dport 50000 -m comment --comment "$TAG" -j ACCEPT)
rule_ping=(-i "$IFACE" -p icmp --icmp-type echo-request -m comment --comment "$TAG" -j ACCEPT)
if [[ ${1:-setup} == cleanup ]]; then
  if iptables -w -C INPUT "${rule_udp[@]}" 2>/dev/null; then iptables -w -D INPUT "${rule_udp[@]}"; fi
  if iptables -w -C INPUT "${rule_ping[@]}" 2>/dev/null; then iptables -w -D INPUT "${rule_ping[@]}"; fi
  echo 'Removed only the two temporary Stage-1 firewall rules.'
  exit
fi
mkdir -p "$ROOT/logs/privileged"
chown "$SUDO_UID:$(id -g "$SUDO_USER")" "$ROOT/logs/privileged"
stamp=$(date +%Y%m%d-%H%M%S)
log="$ROOT/logs/privileged/setup-$stamp.log"
{
  date --iso-8601=seconds
  ip -br addr show dev "$IFACE"
  iptables -w -S INPUT
  lsmod | awk '/^(cp210x|usbserial|cdc_acm) /'
  # Runtime only. The persistent NixOS firewall configuration is untouched.
  iptables -w -C INPUT "${rule_udp[@]}" 2>/dev/null || iptables -w -I INPUT 1 "${rule_udp[@]}"
  iptables -w -C INPUT "${rule_ping[@]}" 2>/dev/null || iptables -w -I INPUT 1 "${rule_ping[@]}"
  # Fixed argv in root-owned transient units: no later sudo or writable script.
  systemd-run --quiet --collect --on-active=4h --unit "$TAG-udp-cleanup-$stamp" \
    /run/current-system/sw/bin/iptables -w -D INPUT "${rule_udp[@]}"
  systemd-run --quiet --collect --on-active=4h --unit "$TAG-ping-cleanup-$stamp" \
    /run/current-system/sw/bin/iptables -w -D INPUT "${rule_ping[@]}"
  # Capture only the dedicated AP wire; tcpdump drops to the invoking user.
  systemd-run --quiet --collect --unit "$TAG-capture-$stamp" \
    --property=RuntimeMaxSec=90 \
    "$TCPDUMP" -i "$IFACE" -nn -s 256 -U -Z "$SUDO_USER" \
    -w "$ROOT/logs/privileged/ethernet-$stamp.pcap" \
    'arp or icmp or icmp6 or (udp and (port 67 or port 68 or port 5353 or port 1900))'
  echo 'Capture runs for 90 seconds in a transient service; this terminal is free now.'
} > "$log" 2>&1
chown "$SUDO_UID:$(id -g "$SUDO_USER")" "$log"
cat "$log"
echo "Evidence directory: $ROOT/logs/privileged"
echo 'The two temporary rules expire automatically in four hours (or at reboot).'
echo "Optional earlier cleanup: $ROOT/tools/prepare_privileged.sh cleanup"
