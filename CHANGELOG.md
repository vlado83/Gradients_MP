# Changelog

All notable public releases of Gradients are documented here.

## v1.17 — 2026-08-05

Contest-release candidate for the M5Stack Global Innovation Contest 2026.

### Added

- One-second IMU neutral-position calibration before gameplay.
- Smooth IMU dead zone around the calibrated neutral position.
- In-game IMU recalibration by holding A.
- Host-side client connection timeout and disconnected-player tracking.
- Client-side `CONNECTION LOST / RECONNECTING` overlay.
- Automatic multiplayer rejoin when packet flow resumes.
- Post-game score-history graph for all participating players.
- `Play Again` and `Main Menu` post-game actions without rebooting.
- Target bonus value synchronization from host to clients.
- Diamond-shaped target whose size indicates the current bonus value.
- USB VBUS detection for development-friendly idle power handling.

### Changed

- Single-player can now start regardless of the device's saved HOST or CLIENT role.
- Starting single-player no longer changes the saved multiplayer role.
- Disconnected clients receive neutral input and are removed from the active-player mask until they return.
- Menu and post-game auto power-off are suspended while USB power is present.
- Post-game flow now resets game state in software instead of requiring a device restart.
- Documentation now describes the procedural field, gradient physics, ESP-NOW architecture, calibration, reconnection, and contest-release workflow.
- Release sketch renamed to `Gradients_SP_MP_1_17.ino`.

### Fixed

- Prevented previously joined but offline clients from remaining active indefinitely.
- Prevented a saved CLIENT device from ignoring the single-player selection.
- Prevented the idle timer from powering off a USB-powered device.
- Corrected stale documentation and player-color comments.

## v1.1

### Added

- Boot-time role selection: hold A for HOST, hold B for CLIENT.
- Deterministic C-button pairing from the startup menu.
- Host-side persistent client slots for up to three clients.
- HOST boot + C action for clearing stored host client slots.
- Multiplayer support for one host plus up to three clients.
- Player color assignment: HOST red, Client 1 blue, Client 2 cyan, Client 3 magenta.
- Color-coded top bar with the local player score shown first.
- Static terrain-based startup screen rendered through an `M5Canvas` buffer.
- Battery status display on menu and post-game screens.
- 60-second auto power-off from menu and post-game screens.
- First-pass sound effects with 10% default volume.
- Serial sound and configuration commands.

### Changed

- Replaced the original Lissajous startup graphics with a terrain-themed startup screen.
- Reworked pairing from bump-triggered discovery to explicit C-button pairing.
- Host no longer clears clients on normal boot.
- Paired clients become active only after the host receives their first input packet.
- Startup/menu input supports hardware-button fallback.

### Fixed

- Fixed Arduino/C++ mixed-integer `max()` compilation.
- Fixed Arduino `.ino` prototype generation involving the `SfxId` enum.
- Avoided FreeFont filled bounding boxes on the buffered startup canvas.

## v1.0

- Initial GitHub release-ready version.
- Shared firmware for host and client.
- Runtime role selection through Serial/NVS.
- Serial/NVS MAC pairing.
- ESP-NOW two-device multiplayer.
- Deterministic terrain synchronization.
- Single-player mode.
