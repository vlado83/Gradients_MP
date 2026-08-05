# Gradients v1.17 — Contest Release Candidate

Gradients v1.17 is the polished M5Stack Core2 build prepared for the M5Stack Global Innovation Contest 2026.

## What is Gradients?

Gradients is an IMU-controlled game in which a procedurally generated scalar field produces real forces in the gameplay physics. Players tilt their M5Stack devices to move across the landscape and race toward a time-sensitive target.

One device can run the game in single-player mode, or one Core2 can act as an authoritative ESP-NOW host for up to three client devices. Every device displays the same terrain and synchronized game state without requiring a phone, router, computer, wiring, or external sensors.

## Highlights in v1.17

- Single-player starts correctly from either saved multiplayer role.
- IMU neutral calibration makes the controls comfortable at the player's natural holding angle.
- A smooth dead zone prevents drift.
- Holding A during a match recalibrates the IMU.
- Hosts detect timed-out clients and automatically restore them when packets return.
- Clients display an explicit reconnection overlay.
- Target shape and size communicate the remaining 1000-to-100-point bonus.
- The post-game screen graphs every participant's score over time.
- Players can replay or return to the menu without rebooting.
- Menu and post-game shutdown are disabled while USB power is connected.
- README and architecture documentation have been rewritten for the contest release.

## Hardware

Primary target:

- M5Stack Core2

Multiplayer:

- 1 host
- Up to 3 clients
- Same firmware on every device

## Upgrade notes

Replace the old release sketch with:

```text
Gradients_SP_MP_1_17.ino
```

Arduino users should place it in a folder with the matching name:

```text
Gradients_SP_MP_1_17/Gradients_SP_MP_1_17.ino
```

Saved HOST/CLIENT roles and paired MAC addresses remain in the existing `gradcfg` NVS namespace. Use `RESETCFG` in Serial Monitor only when a clean configuration is desired.

## Verification status

The source has been checked for:

- consistent v1.17 naming
- merge-conflict markers
- TODO/FIXME markers
- balanced braces and parentheses using a lexical static check
- stale v1.1/v1.13 release references in the prepared documentation

A complete compile and physical multi-device test must still be performed in the author's final Arduino/M5Stack environment before tagging the release.
