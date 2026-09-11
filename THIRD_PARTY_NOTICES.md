# Third-party notices

No license has yet been selected for this project's original code. Publishing the repository does not change the separate terms of its dependencies, vendored code, or brand assets.

## Vendored decoder

[`firmware/devices_badge/vendor/stb_image.h`](firmware/devices_badge/vendor/stb_image.h) is stb_image **2.30**, from [nothings/stb](https://github.com/nothings/stb) at revision `2c980bb59875b0d32144a71867fbdebb2f77cd20`.

- License: MIT or public domain, at the recipient's choice, as specified in the header.
- Both upstream license texts and the upstream attribution remain in the vendored header.
- SHA-256: `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`.
- The project's wrapper enables bounded JPEG-only decoding and supplies PSRAM allocation functions; see the [vendor notes](firmware/devices_badge/vendor/README.md).

## Build dependencies

ESP32 Arduino, M5Unified, M5GFX, ArduinoJson, and their bundled components retain their upstream licenses. They are installed separately and are not relicensed by this repository.

- [Arduino core for ESP32](https://github.com/espressif/arduino-esp32)
- [M5Unified](https://github.com/m5stack/M5Unified)
- [M5GFX](https://github.com/m5stack/M5GFX)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)

## Brand assets

`init_wordmark.h` contains a raster mask derived from the `init()` wordmark on the [WorkOS conference website](https://workos.com/init). `github_mark.h` contains a raster mask of the official GitHub Invertocat from the [GitHub brand toolkit](https://brand.github.com/foundations/logo). The other provider identifiers and layouts refer to LinkedIn and X.

WorkOS, AuthKit, Pipes, `init()`, M5Stack, LinkedIn, X, GitHub, and their respective marks belong to their owners. Inclusion here is not a trademark license, endorsement, or grant of rights to reuse brand assets. The dependency licenses do not license those marks.

## Public certificate roots

The firmware bundles public server-trust certificates sourced from [Google Trust Services](https://pki.goog/roots.pem), [Let's Encrypt](https://letsencrypt.org/certificates/), and [DigiCert Global Root G2](https://cacerts.digicert.com/DigiCertGlobalRootG2.crt). These are public trust anchors, not private keys or client credentials.
