# Hardware and interaction notes

This project targets the **M5Stack StopWatch**, not an arbitrary ESP32 development
board. These notes preserve findings from the September 2026 device sessions.
Current firmware and the pinned dependencies in [README](../README.md) are the
implementation reference; historical measurements are not guarantees for a new
build or another device.

Manufacturer references supplied for this project (SKU **C152**):

- [Stopwatch Dev Kit product page](https://shop.m5stack.com/products/m5stack-stopwatch-dev-kit-esp32-s3?variant=48862593515777)
- [StopWatch hardware documentation](https://docs.m5stack.com/en/core/StopWatch)

Checked September 11, 2026. Use these for schematics, pin maps, expansion, and
hardware-revision notes; use the project observations below for firmware behavior.

## Board and display

| Item | Established configuration |
| --- | --- |
| Controller | ESP32-S3R8, dual-core Xtensa LX7 |
| Flash | 16 MiB / 16,777,216 bytes, confirmed on the development device |
| PSRAM | 8 MiB / 8,388,608 bytes; use **OPI** mode |
| Screen | Round 1.75-inch AMOLED, CO5300 over QSPI |
| Display coordinates | **468 × 468** in the pinned M5GFX driver and device framebuffer |
| Touch | CST820B; accessed through M5Unified/M5GFX |
| Orientation sensor | BMI270 six-axis IMU; the badge uses acceleration |
| Network | 2.4 GHz Wi-Fi |
| Power | USB-C, M5PM1 power management, nominal 450 mAh battery; separate power button |
| NFC | No built-in NFC on this StopWatch |

The manufacturer's nominal display resolution is **466 × 466**. Keep the working
468 × 468 coordinate system: M5GFX configures that size with an internal panel
offset. Its StopWatch driver also explicitly requires OPI PSRAM. The same
driver's board detection distinguishes StopWatch's touch controller and absence
of NFC from the PaperMono family. Relevant upstream source:
[M5GFX `src/M5GFX.cpp`](https://github.com/m5stack/M5GFX/blob/0.2.26/src/M5GFX.cpp)
(`Panel_StopWatch` and the StopWatch detection/configuration block).

The chip, panel, touch controller and radio specifications above agree with the
[official StopWatch hardware reference](https://docs.m5stack.com/en/core/StopWatch).
The framebuffer size and installed memory were also observed on the development
device. A framebuffer capture includes corners hidden by the round screen: QR
verification has used both the full image and a 232-pixel-radius circular
aperture. Preserve quiet zones and keep useful content inside the visible circle.

The product documents ES8311 audio, an AW8737A speaker amplifier, RX8130CE RTC,
and vibration hardware. This firmware enables the
internal IMU and disables internal RTC, microphone, and speaker initialization.
Those other features and battery runtime have not been validated by this badge
project. The board's power controller handles its separate power button.

## Pins and controls

M5Unified owns hardware initialization. Prefer its APIs instead of adding a
second display, touch, or IMU driver.

| Connection | Mapping |
| --- | --- |
| Yellow pusher | `M5.BtnA`, GPIO 2, active low |
| Blue pusher | `M5.BtnB`, GPIO 1, active low |
| Internal I²C | SDA GPIO 47, SCL GPIO 48 |
| BMI270 | I²C address `0x68` |
| Touch interrupt | GPIO 13 |

These pin assignments are in the
[manufacturer's pin map](https://docs.m5stack.com/en/core/StopWatch#pinmap) and
the pinned M5Unified/M5GFX implementations. Display and touch resets go through
the board's I/O expander; do not assume direct ESP32 reset GPIOs. The physical
board revision has not been established. The official reference identifies a
v1.0 sticker error: a pin marked `BAT` can actually be a **5V input**, where a
battery must not be connected. On v1.0.1 that pin is a battery connection.
Confirm the revision and pin map before wiring rear expansion power.

The current interaction contract is intentional:

- Blue advances LinkedIn → X → GitHub, skipping unavailable accounts.
- Yellow is reserved and leaves the badge unchanged.
- One simultaneous press of both pushers opens Settings. This is a **chord**, not
  a double-click. Either pusher returns from Settings to the badge.
- A completed short touch toggles the expanded QR. There is no settings cog.

[`button_gesture.h`](../firmware/devices_badge/button_gesture.h) allows 125 ms for
the second pusher to join a chord. A single action commits on release or after
that interval, then both buttons must be released before another action. This
sits above M5Unified's normal button debouncing (10 ms in version 0.2.19).

Touch dispatch in
[`devices_badge.ino`](../firmware/devices_badge/devices_badge.ino) uses
`wasClicked()`, **after release**. An earlier change to `wasPressed()` caused
unwanted activations and was reverted at the user's request. Keep completed-tap
handling. M5Unified's unchanged defaults recognize holds after 500 ms and drags
using an 8-pixel threshold; those recognized gestures do not become badge taps.
Do not confuse the pusher chord grace interval with touch sensitivity.

## Lanyard orientation

The original stopwatch orientation has the loop at the bottom. Hanging from the
lanyard puts the loop at the top and needs **rotation 2 / 180 degrees**. The
device's loop-up physical calibration produced native accelerometer X near
`+1 g`; physical movement subsequently produced 2 → 0 → 2 rotations, and the
user confirmed the display looked correct.

The important axis mapping is **native Y → screen X, native X → screen Y**.
`updateOrientation()` deliberately calls:

```cpp
orientation.update(data.accel.y, data.accel.x, data.accel.z, now, touching);
```

This matches the axis swap in the
[factory IMU implementation](https://github.com/m5stack/M5StopWatch-UserDemo/blob/main/main/hal/hal_imu.cpp).
The accelerometer's stationary support-force reading points opposite gravity;
do not reverse the mapping by assuming it is a gravity vector.

[`orientation_filter.h`](../firmware/devices_badge/orientation_filter.h) keeps
rotation stable while the device hangs or is handled:

- Poll no faster than every 50 ms; require 700 ms of a stable candidate.
- Smooth X/Y with a 0.25 low-pass coefficient. Require a dominant axis of at
  least 0.65 g and 1.35 times the other axis.
- Reject nonfinite readings and acceleration magnitude outside 0.7–1.3 g.
- Cancel settling after a missing sample, a gap over 250 ms, an ambiguous fresh
  direction, or touch contact/release detail. A flat or diagonal pose keeps the
  last orientation.
- Change display rotation only after touch dispatch and full release. M5GFX
  transforms subsequent touch coordinates; do not apply another manual rotation.

All four cardinal orientations are supported. Rotation redraws local cached
data and does not trigger a profile request or save rotation to flash.

## Build target, USB, and flash layout

[`scripts/build.sh`](../scripts/build.sh) fixes the working target:

```text
esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi
```

[`scripts/flash.sh`](../scripts/flash.sh) adds `UploadSpeed=460800`. The sketch
sets `fallback_board` to `board_M5StopWatch`; the pinned M5GFX enum identifies it
as board 30. Keep the explicit target even if Arduino USB discovery offers a
generic or ambiguous board name. USB identification alone is not the board's
display/memory configuration.

The macOS `/dev/cu.usbmodem…` port has changed after reconnecting. Rediscover it
for each hardware session, close any serial monitor before upload, and do not
copy a historical port into automation. Runtime diagnostics use 115200 baud.
USB output must stay nonblocking: unread output previously affected input-loop
responsiveness. Current capture output has a five-second deadline and restores
nonblocking output afterward.

The following table comes from ESP32 Arduino core **3.3.10**,
`tools/partitions/app3M_fat9M_16MB.csv`:

| Partition | Type / subtype | Offset | Length | Capacity |
| --- | --- | --- | --- | --- |
| `nvs` | data / nvs | `0x9000` | `0x5000` | 20 KiB |
| `otadata` | data / ota | `0xE000` | `0x2000` | 8 KiB |
| `app0` | app / ota_0 | `0x10000` | `0x300000` | 3 MiB |
| `app1` | app / ota_1 | `0x310000` | `0x300000` | 3 MiB |
| `ffat` | data / fat | `0x610000` | `0x9E0000` | 9.875 MiB |
| `coredump` | data / coredump | `0xFF0000` | `0x10000` | 64 KiB |

**Preserve this layout.** The profile store mounts **LittleFS** in the existing
partition named `ffat`; that historical label does not mean this firmware uses
a FAT filesystem. Its address and length are explicitly checked in
[`profile_store.h`](../firmware/devices_badge/profile_store.h). A second app slot
exists, but wireless firmware updates are not implemented. Normal component
upload preserves saved user state; a full erase or full-flash restore does not.

Each provider's 400 × 400 RGB565 avatar uses 320,000 pixel bytes. Three retained
avatars use 960,000 bytes, with another 320,000-byte scratch canvas during an
avatar refresh, plus decoder and other allocations. These are accounting facts,
not measured peak RAM. Build size and free-memory snapshots change with the
firmware; use current build output and bounded diagnostics instead of old release
numbers. See [architecture](architecture.md) for cache behavior, startup
measurements, and validation limits.
