# Initial state — 2026-09-08 23:11 JST

- Discovery ran `find . -maxdepth 2 -name .git -type d -print`: no repositories.
- Only supplied project: `/home/hotaru/silex/halow_stage1_main`, 3.1 MiB.
- All requested Git status/branch/log queries initially failed: no `.git`.
- Preserved supplied source with initial main commit `69115b3`.
- Development worktree: `/home/hotaru/silex/_codex_work/worktrees/stage1-real-hw-20260908-231300`.
- Branch: `codex/stage1-real-hw-20260908-231300`.
- No pre-existing Git history or uncommitted changes were deleted, stashed, or reset.
- ESP USB VID:PID `303a:1001`, serial `90:70:69:10:C7:B4`.
- SX USB-UART VID:PID `10c4:ea60`, serial `08db3e3b2974ea11adf500fb6a30b0f3` (board identity awaits console).
- Both serial devices already readable/writable via `dialout` group.
- Wired interface `enp0s31f6` has carrier, no IPv4 address; NetworkManager Profile 1 uses DHCP.
- Wi-Fi default route `192.168.10.1`, local `192.168.10.136/24`; keep operational.
- First `sudo -v` requested fingerprint, timed out, then awaited password. Never request password through chat.

## Temporary Ethernet setup

- NetworkManager permissions allow own/system temporary connections without sudo.
- Created memory-only `codex-stage1-temp-20260908` UUID `0eceac39-a8a2-454f-a370-b92967ad3eab`, interface `enp0s31f6`.
- Addresses: `169.254.111.100/16`, `192.168.1.250/24`; no default route, IPv6 disabled on this test profile.
- Original persistent `Profile 1` UUID `dd0dc499-289a-48f4-a0e7-089e200e3eb6` unchanged.
- Restore: `nmcli connection up 'Profile 1'`, then `nmcli connection delete codex-stage1-temp-20260908`.
- Wired carrier alone does not establish AP identity or IP connectivity.
- Initial `169.254.111.111`, `192.168.1.1`, `192.168.1.100` ARP/ping: no response.
- Source for international-model default address only (JP/actual config not inferred): https://www.silextechnology.com/hubfs/Resource%20PDF/140-20166-100%20AP-100AH%20user%20manual.pdf, p.18.
- SX UART passive capture and newline/help/AT/sf size/sf memmap at 115200 8N1: zero received bytes. Firmware identity and boot remain unknown.

## Additional live findings

- Temporary DHCP-server mode on `192.168.77.1/24` from 23:27:42 for approximately four minutes: no lease or peer reply. Reverted to manual test addressing.
- Current temporary addresses: `169.254.111.100/16`, `192.168.1.250/24`, `192.168.50.10/24`.
- Repository example AP-side subnet `192.168.50.0/24` tested at `.1`: no ARP reply.
- ESP `flash_id`: manufacturer c8, device4017, actual8MiB quadFlash, eFuse3.3V (read only).
- User later confirmed antennas/commonGND and SW1 `L H L L L H`; no ESP-to-SX 3.3V power rail is connected (correct for separate USB power).
- SW1 value matches documented Linux hosted mode, explaining why XIP Standalone boot has not been established.
- Remaining physical input: EVK host I/O voltage/configuration, then manual power-off/SA switch/power-on.
- User requested all privileged operations in shell script; `tools/prepare_privileged.sh` prepared. Earlier interactive sudo wait ended without authentication.
- At23:40:25 the user successfully executed privileged setup. The pcapture revealed AP sourceMAC `1c:bc:ec:1b:30:fe` announcing192.168.10.10.
- Superseding temporary network: wired192.168.10.250/32 plus direct192.168.10.10/32 route, Wi-Fi default route retained. AP ping3/3 and HTTP200, actual AP-100AH UI identified.
- SX currentFW remains unknown; no `SA ready` confirmation yet and latest 115200 CLI capture still received0 bytes.
- AP reset authorization explicitly granted by user; no reset performed.
