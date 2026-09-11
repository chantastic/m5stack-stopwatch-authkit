# Development and device checks

Read [hardware.md](hardware.md) and [decisions.md](decisions.md) before changing
board configuration, input handling, account behavior, or rendering. This guide
records the workflow established with the physical StopWatch; it does not assume
the device is currently attached or authenticated.

## Source, build, and upload

The active sketch is `firmware/devices_badge/`. The repository has moved between
directories; use paths relative to the checkout. Older source copies beside the
original backup are historical. They are not the working firmware or backend.

1. Check `git status` and read the relevant source. Keep unrelated work intact.
2. Use the pinned dependencies in [README.md](../README.md). Run
   `./scripts/build.sh` for firmware edits and the relevant host checks with
   `./scripts/test.sh`. Documentation-only changes need neither a rebuild nor a
   device flash.
3. If the task calls for updating the board, identify its current port with
   `arduino-cli board list`. The displayed board name can be ambiguous; the build
   scripts carry the verified board options. Close other serial consumers.
4. `./scripts/flash.sh PORT` rebuilds and uploads the normal components. Check
   successful write/hash verification, then verify the behavior on the board.
   Do not equate compilation with installation or installation with UI verification.

The build writes to `.build/firmware/`; test output also stays under `.build/`.
The scripts locate the checkout themselves and accept an `ARDUINO_CLI` override.
Host tests currently require macOS CommonCrypto, `clang++`, Python 3, and the
ArduinoJson include directory described in the README. Generated scheduler
fixtures and source hashes are disposable outputs, not source files to edit.

Ordinary component uploads preserve the established NVS/file partitions and
saved user state. Do not erase flash, change the partition scheme, write security
eFuses, or use a full-device image as a routine update. A missing serial port is
not a firmware failure; check the connection and enumerate again before recovery.

## Verification proportional to the change

The host suite covers account paging, all prior saved-style combinations, button
gestures, orientation, profile/media URL rules, storage failure handling, HTTPS
ownership, and the extracted authentication/profile scheduler. It uses synthetic
sessions and simulated services. It does not emulate physical touch, radio, or
display hardware.

For a layout/control change, check each connected account's normal and expanded
QR, the actual circular screen crop, blue account order, yellow behavior, and
both-button Settings entry with either-button return. Review the rendered faces
for clipped names and QR quiet zones. On this firmware, a style is always zero;
provider design IDs are X=0, LinkedIn=1, GitHub=2. These design IDs differ from the
blue-button page order. Do not use old 9- or 18-design capture scripts unchanged.

For persistence/auth changes, additionally check saved data immediately after
reboot, an offline boot, normal reconnection, and correct user/workspace ownership.
An unchanged reconnect should reuse avatars and skip cache rewrites. Restore the
user's selected account and normal badge at the end of a diagnostic session.

Diagnostics run through the same dispatch functions as the controls, but do not
measure physical button/touch timing. A main-loop maximum includes whatever work
the measurement window contains; screen capture, decode, and storage can inflate
it. Earlier polling measurements excluded network/capture work and must not be
presented as whole-system latency. USB acknowledgment time includes transport
overhead. No battery-life or real flash power-cut reliability result is established.

## USB diagnostic protocol

Use one serial connection at **115200 baud**. Send one JSON object per line.
The existing tested tools use their library's default DTR/RTS behavior; do not
force modem-line changes just to inspect the device. Opening another monitor
while uploading or capturing causes contention.

| Request | Purpose |
| --- | --- |
| `{"op":"status"}` | Status group plus a brief period of periodic reporting |
| `{"op":"status","reset_metrics":true}` | Begin a fresh main-loop-gap measurement |
| `{"op":"orientation"}` | Current sensor/rotation observation |
| `{"op":"button","value":"blue","remember":false}` | Diagnostic account paging; `yellow` and `both` are also accepted |
| `{"op":"provider","value":"github","remember":false}` | Select a cached provider; unconnected providers open their setup tab |
| `{"op":"badge","expanded":true}` | Expanded profile QR; `false` returns to the normal badge |
| `{"op":"capture_badge"}` | Raw RGB capture of the public badge screen |
| `{"op":"refresh_profile"}` | Request real authenticated profile refreshes |
| `{"op":"reboot"}` | Normal restart, retaining saved settings |
| `{"op":"reboot","offline_once":true}` | One test boot with the radio off; the following normal reboot restores Wi-Fi |

`remember:false` prevents diagnostic selection changes from replacing saved
preferences. It is the default, but specify it explicitly in test tools. An
offline test consumes a one-shot NVS flag; it does not delete Wi-Fi credentials.

Status arrives as `DEVICE_STATUS`, `BADGE_STATUS`, `BADGE_STORE`, and orientation
lines. Parse a fresh complete group; do not combine a new connection line with
old queued badge state. `DEVICE_STATUS` includes a user identifier, so retain
only an explicit allowlist of needed fields instead of recording raw output.
`BADGE_STORE` reports counters, not profile contents. See [privacy.md](privacy.md).

Wait for the matching `BADGE_ACTION` or `BADGE_DESIGN` acknowledgment before
checking the resulting state. A historical test failed by observing queued state
before its design command completed. Track `input_presses`: physical interaction
during an automated check invalidates its comparisons. Restart markers also
invalidate a check unless the restart was expected.

Capture is accepted only on the badge screen, never Wi-Fi/sign-in screens. The
stream is `BADGE_CAPTURE width height`, followed by exactly `width × height × 3`
RGB bytes, then `BADGE_CAPTURE_END` or `BADGE_CAPTURE_ABORTED`. Read the binary
length exactly, not line-by-line. Reject incomplete/aborted frames and validate
dimensions. The device caps capture time and temporarily permits short USB
write waits; normal serial writes use a zero timeout to avoid input stalls from
an unread monitor. Keep captures and sanitized test reports outside Git.

## Troubleshooting and recovery

- **Missing avatar:** distinguish missing provider connection, failed HTTPS/trust,
  rejected URL, bounded download failure, and JPEG decoding. Progressive JPEG
  support is intentional. Keep hostname verification and bearer isolation intact.
- **Slow or missed input:** inspect main-loop/worker ownership and unread USB
  output first. Do not switch back to contact-on-press touch behavior; that caused
  accidental activation. See the historical fix in [decisions.md](decisions.md).
- **Badge visible but AuthKit disconnected:** saved offline data is expected.
  Look at network/auth status independently; do not set authenticated from cache.
- **Wi-Fi setup:** use Settings and the device's local captive portal. Credentials
  belong on the device, not in chat, source, command history, or firmware assets.
- **Cache mount failure:** the code deliberately avoids reformatting after the
  first initialization attempt. An interrupted first initialization can need
  explicit repair; ordinary online/RAM operation still works. Do not add an
  unconditional format-on-error shortcut.
- **Factory restoration:** a pre-modification full-flash backup exists for the
  original board. Its private location and restore notes are recorded locally.
  Restoring it replaces current applications, partitions, and saved data; it is a
  separate recovery operation, not routine flashing. Verify the backup's checksum
  and current port before using those instructions.

## Evidence baseline

The September 11, 2026 init-only release (`76eac2c`) compiled to **1,516,455
application bytes** with **52,808 static/global bytes**, in a 3,145,728-byte app
slot. The complete host suite passed with address/undefined-behavior sanitizers,
and upload hashes were verified.

The device run checked nine normal/expanded/offline frames and 18 QR decodes
(full frame plus circular aperture). All three offline frames exactly matched
their online references. Cache startup measured **1,260 ms offline / 1,300 ms
normal**. Reconnection made three profile checks, zero avatar downloads, zero
cache writes, and three unchanged-write skips. Settings dispatch, reserved yellow,
selected-account preservation, and removal of old styles survived restart.

Private evidence is discoverable through `.local/project-context.md` when
present. Its absence on another checkout does not mean those hardware checks have
been rerun there. Update this baseline after relevant future device verification.
