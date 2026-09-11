# Project guidance

- Active firmware is the Arduino sketch in `firmware/devices_badge/`.
- This prototype intentionally connects to the existing chan.dev Production
  Devices application. The backend lives in a separate repository.
- Keep public client IDs and HTTPS endpoint configuration separate from secrets.
  Wi-Fi credentials, session tokens, and cached profiles are provisioned or saved
  on the device, never baked into source.
- Keep full-flash backups, NVS dumps, live diagnostic logs, screenshots, and
  generated binaries outside Git. Build and test output goes in `.build/`.
- `scripts/build.sh` compiles; `scripts/test.sh` runs the host checks on macOS.
  Use the dependency versions in README.md.
- `scripts/flash.sh PORT` rebuilds and uploads application components. Firmware
  changes should preserve the partition layout and saved user state.
- Avatar and profile behavior must remain dynamic. Avoid static personal assets.
- Keep one `init()` ASCII layout for each account. Blue changes accounts; yellow
  is reserved for future styles and currently leaves the badge unchanged. Both
  pushers open Settings, and either pusher returns from Settings to the badge.
  Touch uses completed taps on release; keep orientation handling intact.
