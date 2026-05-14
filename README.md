# Gradients v1.1

Gradients v1.1 is the second release-ready version of the M5Stack multiplayer terrain game. It expands the original ESP-NOW two-device prototype into a more robust no-PC multiplayer workflow with boot-time role selection, deterministic C-button pairing, up to four players, persistent host client slots, battery-aware menu/post-game behavior, buffered terrain-based startup graphics, and first-pass sound effects.

## Highlights

- **One firmware for all devices**  
  Upload the same `.ino` to the host and to every client.

- **Boot-time role selection**  
  Hold **A while booting** to force/save `HOST`.  
  Hold **B while booting** to force/save `CLIENT`.  
  If no boot button is held, the device uses the previously saved role from NVS.

- **Deterministic no-PC pairing**  
  Pairing is now done from the startup menu by pressing **C on the HOST and C on one CLIENT**. This avoids the accidental multi-device pairing behavior that can happen with bump-only discovery.

- **Persistent host client list**  
  The host stores paired clients in NVS. Stored clients are kept across normal reboots.

- **Explicit host client reset**  
  Hold **C while booting a HOST** to clear the stored host client list. This is ignored on CLIENT devices.

- **True multiplayer first pass**  
  Supports one host and up to three clients:

  | Player | Role | Color |
  |---|---|---|
  | H | Host | Red |
  | C1 | Client 1 | Blue |
  | C2 | Client 2 | Cyan |
  | C3 | Client 3 | Magenta |

  The host runs the authoritative game simulation and sends the synchronized game state to all active paired clients.

- **Color-coded multiplayer HUD**  
  The top bar shows the local player score first, drawn in that player's color. The second displayed score is the top competitor, or the second-place player when the local player is currently leading.

- **Buffered terrain startup screen**  
  The Lissajous menu graphics were replaced with a static random terrain background rendered into an `M5Canvas` buffer to avoid flicker. Startup text uses a black foreground with a white offset highlight for readability.

- **Battery display and auto power-off**  
  The menu and post-game screen show battery status. If the device remains in the menu or post-game screen for 60 seconds, it powers off automatically.

- **Sound effects**  
  First-pass SFX are included for boot, game start, successful pairing, score events, game over, and auto power-off. Default SFX volume is 10%.

- **Serial configuration remains available**  
  Serial commands are still available for diagnostics, role configuration, pairing fallback, client-slot inspection, volume control, and reset.

## Target hardware

The sketch is configured for 320 × 240 px M5Stack devices, especially:

- M5Stack Core2
- M5Stack CoreS3
- M5Stack Core / Fire style devices with button input, with possible display/input adjustments

Other M5 devices may require display-size, input, power-management, or speaker adaptation.

## Dependencies

Arduino libraries / platform support:

- `M5Unified`
- `M5GFX`, normally installed with M5Unified
- ESP32 Arduino / M5Stack board package
- ESP-NOW support from the ESP32 core

Main includes used by the sketch:

```cpp
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_system.h>
#include <esp_idf_version.h>
```

## Uploading

Upload the same firmware file to all devices:

```text
Gradients_SP_MP_1_1.ino
```

The compile-time role define is only a fallback:

```cpp
#define DEFAULT_IS_HOST_DEVICE 1
```

The active role is loaded from NVS at runtime unless changed during boot or through Serial commands.

## Normal no-PC setup

### 1. Choose the host

Power or reset the intended host while holding **A**.

Result:

```text
HOST role is saved in NVS.
```

### 2. Choose each client

Power or reset each client while holding **B**.

Result:

```text
CLIENT role is saved in NVS.
```

### 3. Pair each client with the host

On the startup screen, press **C on the HOST and C on one CLIENT**.

Repeat once for each client:

```text
HOST + Client 1 -> C1
HOST + Client 2 -> C2
HOST + Client 3 -> C3
```

The host accepts only one new client per C-button pairing window. This keeps pairing deterministic when several devices are powered.

### 4. Start multiplayer

Press **B: Multi** on the host and on the clients.

Paired clients become active only after the host receives their first input packet, so powered-off paired clients should not appear as ghost players.

## Clearing host clients

To clear all stored client slots on the host:

1. Make sure the device is already saved as HOST.
2. Hold **C while booting**.
3. Release the boot button when prompted.
4. Pair clients again from the startup menu.

Normal host boot keeps the client list.

## Menu controls

Startup/menu screen:

```text
A: Singleplayer
B: Multi
C: Pair HOST + CLIENT
```

Boot-time controls:

```text
Hold A during boot: save HOST role
Hold B during boot: save CLIENT role
Hold C during HOST boot: clear stored HOST client slots
```

## Serial monitor setup

Open Serial Monitor at:

```text
115200 baud
Newline enabled
```

Useful commands:

```text
HELP
CFG?
MAC?
ROLE?
ROLE HOST
ROLE CLIENT
PEER 84:1F:E8:85:30:48
PEER 841FE8853048
PAIR HOST <client-mac>
PAIR CLIENT <host-mac>
PAIR?
CLIENTS?
RESETCFG
REBOOT
VOL?
VOL 10
SFX?
SFX ON
SFX OFF
```

## Pairing notes

The preferred workflow is now C-button pairing. Serial pairing remains as a fallback/debug path:

```text
PAIR HOST <client-local-mac>
PAIR CLIENT <host-local-mac>
```

The MAC used in a `PAIR` command is always the other device's local MAC address.

## Gameplay flow

### Host device

- Can start singleplayer or multiplayer.
- Generates the terrain seed.
- Runs the authoritative game simulation.
- Stores up to three paired client MAC addresses.
- Receives client input packets.
- Sends synchronized `GameState` packets to active clients.

### Client device

- Starts multiplayer.
- Waits for the first host `GameState`.
- Recreates the terrain using the host-provided seed.
- Sends local IMU tilt input to the host.
- Stores the host MAC address in NVS.

## Sound FX

Default sound volume is 10%.

Serial commands:

```text
VOL?       show current volume
VOL 10     set volume to 10%
VOL 0      mute by volume
VOL 100    maximum volume
SFX?       show SFX state
SFX ON     enable SFX
SFX OFF    disable SFX
```

SFX volume and enable state are not currently saved to NVS; they reset after reboot.

## Battery and auto power-off

The menu and post-game screen display battery status. If the device remains inactive for 60 seconds in either screen, it powers off automatically.

On USB power, some M5 devices may appear to restart or remain externally powered after shutdown. On battery, the device should power off normally.

## Notes and limitations

- ESP-NOW encryption is currently disabled.
- The current maximum multiplayer configuration is one host plus three clients.
- Host client slots are persistent and must be cleared with HOST boot + C if you want a clean pairing set.
- Clients store only one host MAC address.
- C-button pairing uses ESP-NOW broadcast but only accepts packets while the local C-pairing window is active.
- The current sketch assumes a 320 × 240 display layout.
- Sound effects are intentionally simple first-pass tones.
- SD logging, game modes, score graphs, and advanced post-game analysis are planned for later versions.

## Release files

- `Gradients_SP_MP_1_1.ino` — firmware sketch
- `README.md` — repository overview and setup instructions
- `CHANGELOG.md` — release history
- `RELEASE_NOTES_v1.1.md` — GitHub release text

## License

GPL-3.0. See `LICENSE`.
