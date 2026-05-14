# Changelog

## v1.1

### Added

- Boot-time role selection: hold A for HOST, hold B for CLIENT.
- Deterministic C-button pairing from the startup menu.
- Host-side persistent client slots for up to three clients.
- HOST boot + C action for clearing stored host client slots.
- True multiplayer first pass: one host plus up to three clients.
- Player color assignment: HOST red, Client 1 blue, Client 2 cyan, Client 3 magenta.
- Color-coded top bar with the local player score shown first.
- Static terrain-based startup screen rendered through an `M5Canvas` buffer.
- Battery status display on menu and post-game screens.
- 60-second auto power-off from menu and post-game screens.
- First-pass sound effects with 10% default volume.
- Serial SFX commands: `VOL?`, `VOL <0..100>`, `SFX?`, `SFX ON`, `SFX OFF`.
- Serial host client inspection command: `CLIENTS?`.

### Changed

- Replaced the original Lissajous startup graphics with a terrain-themed startup screen.
- Reworked pairing from bump-triggered discovery to explicit C-button pairing.
- Host no longer clears clients on normal boot; clients are kept unless C is held during HOST boot.
- Pairing vibration now happens once after successful pairing only.
- Paired clients become active only after the host receives input, avoiding ghost players.
- Startup/menu input now supports robust hardware button fallback.

### Fixed

- Fixed Arduino/C++ `max(int, int32_t)` compile issue in game-state time-left calculation.
- Fixed Arduino `.ino` preprocessor issue by declaring the SFX enum before `playSfx()`.
- Avoided FreeFont filled bounding boxes on the buffered startup canvas by using single-argument `setTextColor()` for highlighted text drawing.

## v1.0

- Initial GitHub release-ready version.
- Single shared firmware for host and client.
- Runtime role selection through Serial/NVS.
- Serial/NVS-based MAC pairing.
- ESP-NOW two-device multiplayer.
- Deterministic terrain synchronization.
- Singleplayer mode.
