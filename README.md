# Cardputer-Adv SSH Firmware

Native ESP-IDF firmware for M5Stack Cardputer-Adv. The device scans and stores Wi-Fi networks, stores SSH server profiles, and connects to a remote SSH server with password authentication from the built-in keyboard.

## Status

This repository is the first structured firmware implementation pass:

- ESP-IDF project scaffold for ESP32-S3 / 8 MB flash.
- Wi-Fi manager with scan, profile storage, auto-connect, and reconnect.
- SSH client wrapper for password auth and command execution.
- Cardputer-Adv UI shell for Wi-Fi setup, SSH profile setup, and command entry.
- Display and keyboard adapters isolated behind small interfaces.

The local machine currently needs ESP-IDF installed before build/flash can run.

## Build

Install ESP-IDF 5.4 or newer, then:

```bash
idf.py set-target esp32s3
idf.py build
```

## Flash

Cardputer-Adv uses native USB. If flashing cannot auto-enter the ROM loader:

1. Press and hold the rear `BtnG0`.
2. While holding `BtnG0`, tap rear `BtnRST`.
3. Release `BtnRST`, keep holding `BtnG0` for about one second.
4. Release `BtnG0`; the screen should stay dark.

Then flash with the detected port:

```bash
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

## Use

On boot:

1. Saved Wi-Fi profiles are scanned and auto-connected when available.
2. If no saved Wi-Fi works, open `Wi-Fi` and scan nearby networks.
3. Select an SSID, enter the password, and save.
4. Open `SSH`, add or edit a server profile, then connect.
5. Use `Terminal` to run commands over SSH.

Password authentication is implemented first. Device-generated key auth is intentionally left for a later pass.
