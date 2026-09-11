# m5stack-stopwatch-authkit

A connected profile badge for the **M5Stack StopWatch**, using WorkOS AuthKit and Pipes through chan.dev. Sign in on your phone, connect your accounts, and wear a badge with your live name, portrait, and profile QR code.

The firmware supports **LinkedIn, X, and GitHub**, with six styles for each account: an `init()` ASCII portrait, two provider-inspired layouts, and three outdoor emblems—Summit, Grove, and Tide. Portraits and QR codes are generated from the signed-in user's own profile data.

Saved profiles load before networking starts. In the latest hardware check, all three profiles and avatars were ready in about **1.3 seconds**, including a restart with Wi-Fi disabled. When online, the badge refreshes in the background and reuses unchanged avatars.

## Project scope

This is firmware for a personal device, currently configured for chan.dev's **Production Devices** AuthKit application and the existing `https://auth.chan.dev` service. The public client ID is intentionally in the source; passwords, client secrets, and provider tokens are not.

The chan.dev backend is maintained separately and is **not included here**. Building this repository does not create an AuthKit application, configure Pipes providers, or deploy a backend. A clean build still targets the existing chan.dev environment. See [architecture](docs/architecture.md) for the service contract and [privacy](docs/privacy.md) for storage and credential handling.

## Hardware and controls

Designed for the M5Stack StopWatch with an ESP32-S3, 16 MiB flash, 8 MiB PSRAM, and a 468 × 468 round touchscreen.

| Control | Action |
| --- | --- |
| Blue pusher | Next connected account: LinkedIn → X → GitHub |
| Yellow pusher | Next style for the current account |
| Both pushers together | Open Settings |
| Either pusher from Settings | Return to the badge |
| Short tap and release on the badge | Expand or shrink its QR code |
| Turn the device | Automatically rotate the display |

One simultaneous press opens Settings; no double-click is needed. Both pushers must be released before another action. Recognized drags and holds do not trigger a badge tap. Each account remembers its own style, and the last selected account is restored at startup.

## Build and flash

Install [Arduino CLI](https://arduino.github.io/arduino-cli/latest/installation/), then install the versions used for this firmware:

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.10 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install --no-deps "M5Unified@0.2.19" "M5GFX@0.2.26" "ArduinoJson@7.4.3"
```

From the repository root:

```sh
./scripts/build.sh
arduino-cli board list
./scripts/flash.sh /dev/cu.usbmodemYOUR_PORT
```

Replace the port with the StopWatch's port from `board list`. The scripts target ESP32-S3 with hardware USB CDC, 16 MiB flash, the `app3M_fat9M_16MB` partition scheme, and OPI PSRAM. The vendored JPEG decoder needs no separate installation.

The normal component upload preserves this firmware's saved device settings and profile cache. It still replaces the installed application. Make a private backup before replacing factory firmware; no factory image or full-device dump is distributed here.

## First connection

1. Press both pushers, then open **Wi-Fi → Set up Wi-Fi**. Scan the device's QR to join its temporary, password-protected hotspot. Open the captive portal, or visit `http://192.168.4.1`, and enter a 2.4 GHz network. Credentials are entered locally and saved on the device after a successful connection.
2. Open **AuthKit / chan.dev → Tap to connect**. Scan the sign-in QR, use your chan.dev account, and confirm the matching code. The device connects to the Production Devices application.
3. Open **Profile**, choose LinkedIn, X, or GitHub, and scan the Connections QR. Connect the provider using the same chan.dev account, then tap **Refresh accounts**. LinkedIn also needs your public profile link because its sign-in profile does not supply one.

Once saved, badges remain available through an internet outage. Settings distinguishes saved offline data from a current authenticated connection. Refresh runs after startup and when requested; a successful check does not start continuous provider polling.

## Development

The sketch is in [`firmware/devices_badge`](firmware/devices_badge). Most changes belong in its focused headers: profile fetching, persistent storage, background HTTP, button gestures, orientation, and badge rendering.

```sh
./scripts/test.sh
```

Host checks use the production helpers or extracted production functions with simulated device services. They cover account boundaries, URL validation, input and orientation behavior, HTTP ownership, and cache failure handling. See [architecture](docs/architecture.md) for the module map and verification limits.

The current test harness requires **macOS, `clang++`, and Python 3**; its storage fixture uses CommonCrypto. It looks for ArduinoJson at `~/Documents/Arduino/libraries/ArduinoJson/src`. If your Arduino library directory differs, set `ARDUINOJSON_INCLUDE` to its `ArduinoJson/src` directory before running the tests. Build and flash scripts also accept an `ARDUINO_CLI` executable override.

Original project code does not yet have a license selected. Vendored software and brand assets have separate terms; see [third-party notices](THIRD_PARTY_NOTICES.md).
