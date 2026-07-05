# cardputer_adv_ssh

English | [中文](README.zh-CN.md)

Native ESP-IDF firmware for the M5Stack Cardputer-Adv (`cardputer_adv`). It turns the device into a small Wi-Fi SSH terminal with saved server profiles, persistent SSH sessions, on-device settings, and an on-demand local WebUI for easier configuration.

## Features

- Launcher-style UI with Terminal first, plus Wi-Fi, WebUI, SSH profiles, and Status pages.
- Wi-Fi scanning, saved credentials, auto-connect, and reconnect.
- SSH profile storage with default server selection and optional per-session server switching.
- SSH terminal with scrollback review mode, temporary zoom, ANSI colors, theme presets, and CJK font presets.
- Password login and device-generated SSH key login.
- On-demand WebUI for editing SSH profiles, choosing terminal settings, generating/copying the device public key, and setting the default profile.
- Status bar icons for Wi-Fi, SSH state, and battery.
- GitHub Actions workflow for CI builds and tag-based release packaging.

## Hardware And Firmware Target

- Device: M5Stack Cardputer-Adv
- Firmware/project name: `cardputer_adv_ssh`
- MCU target: ESP32-S3
- Flash size: 8 MB
- Framework: ESP-IDF 5.4 or newer

The project uses ESP-IDF component dependencies from `main/idf_component.yml`, including M5Unified/M5GFX and libssh2 for ESP.

## Build Locally

Install ESP-IDF 5.4 or newer, then run:

```bash
idf.py set-target esp32s3
idf.py build
```

The main firmware image produced by the build is `build/cardputer_adv_ssh.bin`.

## Flash

Use the standard ESP-IDF flash command:

```bash
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Cardputer-Adv uses native USB. If it does not enter the ROM bootloader automatically:

1. Press and hold the rear `BtnG0`.
2. While holding `BtnG0`, tap the rear `BtnRST`.
3. Release `BtnRST`, keep holding `BtnG0` for about one more second.
4. Release `BtnG0`; the screen should stay dark.

Then rerun the flash command with the detected port.

## Basic Use

On boot, the launcher opens with Terminal as the first app.

1. Open `Wi-Fi`, scan, select a network, and save its password.
2. Open `SSH Profiles`, add a server in `user@host` or `user@host:port` form, and set it as default.
3. Open `Terminal`; it connects to the default SSH profile when needed.
4. Exit Terminal to return to the launcher. The SSH connection stays alive unless you switch servers or the connection drops.

If another profile is selected while a different SSH connection is active, Terminal asks whether to switch servers or keep the current session.

## WebUI

The WebUI is off by default. Open the `WebUI` page on the device to start it. The screen shows the local URL and a temporary password.

From the WebUI you can:

- Add, edit, delete, and set default SSH profiles.
- Change terminal chrome, theme, and font preset.
- Generate an SSH key pair on the device.
- Copy the device public key for server-side `authorized_keys`.

The private key is saved on the device. The public key is shown in the WebUI so it can be copied to the server.

## SSH Keys

After generating a device key in the WebUI:

1. Copy the public key.
2. Add it to the target server user's `~/.ssh/authorized_keys`.
3. Save an SSH profile for that server.

The firmware tries the stored private key first and falls back to the profile password when needed. A password can be left empty when the server accepts the device key.

## Terminal Controls

- `Esc`: enter scrollback review when scrollback exists.
- `Esc` twice quickly: exit Terminal to the launcher.
- In review mode, `Up`/`Down` scroll through history.
- In review mode, `Left`/`Right` changes the temporary terminal zoom.
- `Enter` exits review mode and returns to the live terminal.

Terminal chrome, color theme, and CJK font preset are saved settings. Temporary zoom is intentionally not saved.

## Fn Layer

`Fn` can be held while pressing another key, or tapped once to apply to the next key.

- `Fn` + `;`: Up
- `Fn` + `,`: Left
- `Fn` + `.`: Down
- `Fn` + `/`: Right
- `Fn` + a letter: sends the matching control character, for example `Fn` + `C` for Ctrl-C, `Fn` + `D` for Ctrl-D, and `Fn` + `L` for Ctrl-L.
- `Fn` + `[`: Esc
- `Fn` + `\` / `]`: `Ctrl-\` / `Ctrl-]`
- `Fn` + `Shift` + `6`: Ctrl-^
- `Fn` + `Shift` + `-`: Ctrl-_

## Release Workflow

The GitHub Actions workflow in `.github/workflows/firmware-release.yml` builds the firmware on pushes and pull requests. Pushing a version tag publishes a GitHub Release:

```bash
git tag v0.1.0
git push origin v0.1.0
```

Release assets include:

- Firmware image: `cardputer_adv_ssh.bin`
- Bootloader image
- Partition table image
- Flash arguments
- SHA-256 checksums
- A source archive for the tagged commit

The workflow can also be run manually with an optional `release_tag`.

## Repository Layout

- `main/` - firmware application code
- `components/libvterm/` - bundled terminal emulator dependency
- `partitions.csv` - 8 MB flash partition layout
- `sdkconfig.defaults` - default ESP-IDF settings
- `docs/` - design notes and architecture documentation
- `.github/workflows/` - CI and release automation

## License

This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE).

## Notes

This firmware is built specifically for Cardputer-Adv. A regular Cardputer has a similar form factor but should not be assumed to use the same firmware without checking its hardware variant.
