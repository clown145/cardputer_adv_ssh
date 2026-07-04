# Architecture

The firmware keeps device IO, network state, SSH state, storage, and UI separate.

## Modules

- `drivers/`: Cardputer-Adv display and keyboard adapters.
- `storage/`: NVS-backed Wi-Fi and SSH profiles.
- `net/`: Wi-Fi scanning, auto-connect, and reconnect state.
- `ssh/`: SSH connection/session operations.
- `ui/`: Menus, text entry, and terminal screen logic.
- `app/`: Application orchestration.

## State Flow

Boot initializes NVS, display, keyboard, network interfaces, and event loops. The app then asks `WifiManager` to connect to the best saved profile. UI remains responsive while Wi-Fi and SSH work run through explicit state objects. Screen code never calls ESP-IDF Wi-Fi or libssh2 directly.

## Near-Term Work

- Confirm M5Unified component names against the local ESP-IDF component manager.
- Build against ESP-IDF 5.4+ and fix any component API drift.
- Validate Cardputer-Adv keyboard events on hardware.
- Extend SSH terminal from command-exec mode toward PTY-like interaction.
