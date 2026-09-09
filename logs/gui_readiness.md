# 360p result and desktop launch readiness

The completed `logs/video-360p-real-20260909/video_result.json` reports PASS: one same-stream cycle of 120 consecutive frames (IDs 0 through 119), with byte-exact source order. Across the 60-second receive window: 1379 packets, 597 completed frames, zero invalid packets, duplicates, out-of-order packets, expired frames, or mismatched frames. The recorded ESP serial log contains zero explicit crash markers.

An independent local comparison of `received_360p.h264` against the concatenated HVC1 reference frame bytes also passed: 263049 bytes, SHA256 `d6167cdd1046e2874bd440ed26026c0886957929a515fa09b6df2cebbf4cdd7c`.

The installed Nix Electron runtime can run the existing receiver source without installing npm dependencies. Its version is 40.10.3, while `receiver/package.json` pins 44.2.0; a launch with this runtime is a compatibility bench run. Decoder availability and actual displayed video still require runtime observation.

After the root agent confirms the byte-PASS gate, the prepared desktop launch is:

```sh
cd /home/hotaru/silex/_codex_work/worktrees/stage1-real-hw-20260908-231300
DISPLAY=:0 \
XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.GB98U3 \
WAYLAND_DISPLAY=wayland-0 \
DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus \
/nix/store/ywr3xyqghdgvvvl3yf2ws742xgd0xhkm-electron-40.10.3/bin/electron \
  --ozone-platform=x11 "$PWD/receiver"
```

These four environment values came from the existing GNOME desktop process using a strict allowlist; no other process-environment values or Xauthority contents were exposed. Their paths are session-specific.

Commit `8f1bafb` sets the GUI defaults to `#bind=192.168.10.250`, `#source=192.168.10.20`, and `#port=50000`. Keep the feedback checkbox off for this observation. The completed byte-check wrapper pauses ESP playback on exit, so the root agent must issue ESP `start` after the GUI is listening. The receiver uses browser WebCodecs for H.264 decoding and does not require an external ffmpeg executable.

For the root agent's loopback-only CDP port 9229, evaluate the following on the `halow://app/` target with `awaitPromise: true`, then require state `UDP 受信中` before ESP `start`:

```js
(async () => {
  await document.getElementById('listen').onclick();
  return {state: document.getElementById('state').textContent,
    bind: document.getElementById('bind').value,
    source: document.getElementById('source').value,
    port: document.getElementById('port').value};
})()
```

The underlying IPC path is `window.halow.listen(cfg)` → `ipcRenderer.invoke('listen', cfg)` → main-process `receiver.start(cfg)`. Calling the existing button handler also updates the user-visible state. Observe `document.body.dataset.displayedFrames` twice to establish actual decoded-frame progress; read `#frames`, `#decoded`, `#codec-label`, `#decoder`, `#picture`, and `#log` for receipt, decode mode, size and errors. `Page.captureScreenshot` on this renderer target captures only the app's web contents.

This readiness task did not launch a GUI, bind a receiver/debug port, alter the desktop, or access either serial device.
