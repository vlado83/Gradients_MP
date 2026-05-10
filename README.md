# Gradients v1.0

**Gradients v1.0** is the first release-ready version of the M5Stack multiplayer terrain game. It supports a single shared firmware for both devices, runtime role selection, ESP-NOW multiplayer, deterministic terrain synchronization, and Serial/NVS-based pairing.

## Highlights

- **One firmware for host and client**  
  The same `.ino` can be uploaded to both devices. The device role is stored in NVS and can be changed over Serial.

- **Runtime host/client configuration**  
  No recompilation is needed to switch a device between `HOST` and `CLIENT`.

- **NVS-persistent peer pairing**  
  Role and peer MAC address are saved using `Preferences`, so the configuration survives normal reboots and sketch uploads.

- **Serial pairing workflow**  
  Devices can be paired with commands such as:

  ```text
  PAIR HOST <other-device-mac>
  PAIR CLIENT <other-device-mac>
  ```

- **On-screen MAC display**  
  The menu screen shows the local MAC address, configured peer MAC address, and current role.

- **ESP-NOW multiplayer**  
  The host simulates the game state and streams it to the client. The client streams its tilt input back to the host.

- **Synchronized randomized terrain**  
  The host generates the terrain seed and sends it to the client, so both devices render the same terrain for each multiplayer run.

- **Singleplayer mode**  
  Available on devices configured as `HOST`.

- **Touchscreen and button menu support**  
  Core2/CoreS3 use touchscreen left/right selection. Core/Fire-style devices use buttons A/B.

- **Arduino-ESP32 2.x / 3.x ESP-NOW callback compatibility**  
  Includes callback-signature compatibility for newer ESP-IDF 5 based board packages.

## Target hardware

The sketch is configured for **320 × 240 px M5Stack devices**, especially:

- M5Stack Core2
- M5Stack CoreS3
- M5Stack Core / Fire style devices with buttons

Other M5 devices may require display-size and input adaptation.

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

Upload the same firmware file to both devices:

```text
Gradients_SP_MP_1_0.ino
```

The compile-time role define is now only a fallback:

```cpp
#define DEFAULT_IS_HOST_DEVICE 1
```

The active role is loaded from NVS at runtime.

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
PAIR HOST <other-device-mac>
PAIR CLIENT <other-device-mac>
PAIR?
RESETCFG
REBOOT
```

## Pairing procedure

Upload the firmware to both devices and power them on.

Each device shows its own local MAC address on the menu screen:

```text
Local MAC: AA:AA:AA:AA:AA:AA
Peer  MAC: BB:BB:BB:BB:BB:BB
Role: HOST / CLIENT
```

On the device that should start and simulate the game, type:

```text
PAIR HOST <client-local-mac>
REBOOT
```

On the second device, type:

```text
PAIR CLIENT <host-local-mac>
REBOOT
```

Example:

```text
PAIR HOST 84:1F:E8:85:30:48
REBOOT
```

```text
PAIR CLIENT 84:1F:E8:85:5F:0C
REBOOT
```

The MAC used in a `PAIR` command is always the **other device's local MAC address**.

## Gameplay flow

### Host device

- Can start **singleplayer** or **multiplayer**.
- In multiplayer, generates the terrain seed.
- Runs the authoritative game simulation.
- Sends `GameState` packets to the client over ESP-NOW.

### Client device

- Starts multiplayer only.
- Waits for the first host `GameState`.
- Recreates the terrain using the host-provided seed.
- Sends local IMU tilt input to the host.

## Notes and limitations

- Pairing is Serial-assisted, not automatic wireless discovery.
- ESP-NOW encryption is currently disabled.
- After changing `ROLE`, `PEER`, or `PAIR`, reboot before starting multiplayer.
- `RESETCFG` clears the saved NVS configuration and reloads compile-time defaults.
- NVS usually survives normal sketch uploads, but may be cleared by full flash erase or partition changes.
- The current sketch assumes a 320 × 240 display layout.

## Release files

- `Gradients_SP_MP_1_0.ino` — firmware sketch
- `Gradients_v1.0_GitHub_Release.md` — release notes / GitHub release text


