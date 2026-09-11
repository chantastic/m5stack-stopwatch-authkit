# Decisions and lessons

Baseline: September 11, 2026, firmware commit `76eac2c`. Current code takes
precedence when a later change updates these decisions.

## Product direction

- This is a wearable social-profile badge on an M5Stack StopWatch. The broader
  goal is small device apps using the signed-in user's data through Pipes.
- The WorkOS application is **Devices**, deliberately broad enough for more than
  this one board. Targeting the existing chan.dev Production environment is fine;
  a configurable setup for other people's environments is not currently required.
- Devices accepts any user who signs into that Production environment. Early
  restrictions to one owner/email were removed. Never restore those restrictions
  or substitute the developer's personal provider connection.
- Implemented accounts are LinkedIn, X, and GitHub. YouTube is a possible next
  integration; Tailscale and Twitch were also discussed. None of those three is
  implemented by this firmware yet.

## Appearance and controls

The chosen face is the original `init()` ASCII portrait for each account. The
provider-native alternatives and outdoor Summit/Grove/Tide experiments were
removed at the user's request. Do not reintroduce them by default. Existing
style values normalize to zero and the normalized preference is saved once.

Blue changes accounts in LinkedIn → X → GitHub order, skipping unavailable
accounts. Yellow is reserved for future styles; it currently leaves both the
normal badge and expanded QR alone. Both pushers pressed together open Settings.
That is one chord, not a double-click. Either single pusher returns from Settings.
The on-screen settings cog was removed when the chord was introduced.

Touch uses M5Unified's completed `wasClicked()` gesture. Acting on first contact
with `wasPressed()` caused accidental taps and was explicitly reverted. Preserve
drag/hold rejection and the requirement to release a touch before rotating.
See [hardware.md](hardware.md) for the calibrated lanyard orientation and timing.

Names, handles, portraits, and QR destinations must come from the signed-in
user's data. The ASCII portrait is generated on the device from its avatar
pixels. Static provider marks and the official `init()` wordmark are intentional;
a baked personal portrait is not. An obsolete avatar asset was excluded when
the repository was created.

## Identity and provider setup

The browser and device use the same chan.dev identities and personal workspaces.
The device endpoints require a valid **Devices** client session. A normal website
session or a visible cached badge is not interchangeable with that session.

Personal Pipes connections are workspace-scoped. When the device lacks an
organization-scoped token, `/devices/workspace` resolves its existing personal
workspace through the authenticated backend. The device then requests a WorkOS
session for that organization. Preserve this verified path: do not invent an
organization, accept arbitrary owner selectors, or fall back to a shared connection.

The following is the configuration established during this session, not a live
dashboard inventory or a claim about current provider pricing:

| Provider | Existing application/flow | Configured scopes and lesson |
| --- | --- | --- |
| X | `chantastic`; OAuth 2.0 web client through Pipes | `users.read tweet.read offline.access`. X's separate “Generate an Access Token” console section is not required for this flow. |
| LinkedIn | `chantastic`; OpenID Connect | `openid profile`. Sign-in supplies identity/photo but not the public profile URL. |
| GitHub | `chan.dev`; OAuth App shared with the wider chan.dev system | No additional scopes for this public profile use. Reuse the existing app; it was intentionally created for more than the badge. |

LinkedIn's public `/in/<slug>/` URL is supplied separately by the user in the
Connections web UI. Never derive it from a name, email, or opaque subject ID.
If absent, retain the name/photo and show explicitly labeled link-setup guidance.
See [architecture.md](architecture.md) for response shapes and current validators.

An X `x_client_not_enrolled` failure was resolved by correcting the developer
app's project enrollment. It was not a reason to rotate every credential or
discard the existing Pipes connection. Inspect the actual error and current
provider configuration before retrying that diagnosis.

The original backend experiment in the historical artifact directory was
superseded. The active backend is maintained separately; local discovery notes
identify it. A prior Cloudflare backend issue was fixed by using manual redirect
handling and rejecting redirects explicitly. Consult that backend's current
implementation before changing its HTTP behavior.

## Avatar, responsiveness, and caching fixes

- X served a **progressive JPEG**, which the display's built-in JPEG decoder did
  not handle. `avatar_decode.h` wraps pinned, JPEG-only `stb_image` and allocates
  in PSRAM. Keep its input/dimension bounds. A JPEG is limited to 128 KiB and
  512 × 512 by that wrapper, even though the HTTP/image-header stages have larger
  bounds. PNGs use the existing M5GFX path. See the source for exact limits.
- A separate RAM profile/avatar exists for each provider. Paging should only
  select an existing canvas. Do not fetch or decode an avatar on every page,
  rotation, redraw, or Settings-tab change.
- HTTPS runs on a dedicated worker so network waits do not block input. The
  main task owns display, application state, Preferences, and cache files.
  Decode, drawing, hashing, and flash commits still run on that task; do not
  describe the entire UI as nonblocking.
- Consume an already-owned authentication result before starting timer-driven
  work. Preserve completed refresh responses even after Wi-Fi drops, because
  they can contain the next rotated refresh token. Pairing must not be starved
  by a due refresh timer. Scheduler regressions cover these cases.
- Saved profiles load before networking and remain visible during transient
  outages. They do not create live authentication. A separate trusted context
  ties the files to the saved user, workspace, client, and provider.
- Reuse avatar pixels only for the same remote identity and unchanged source
  URL. If that user's new image fails, retain the previous image's actual URL
  so a future check retries. Never give a new linked account the old one's photo.
- Cache updates verify a temporary file before atomic replacement. Unchanged
  content skips writing. Durable invalidation prevents deleted or replaced
  identities from returning after a failed filesystem operation.
- Automatic refresh happens after startup; manual **Refresh accounts** remains
  available. Transient failures retry. Successful profile checks do not start
  continuous polling, and session renewal is independent.
- First-time profile responses can finish out of order. LinkedIn or X arriving
  first must not replace a remembered GitHub selection while GitHub is loading.

The detailed storage rules are in [architecture.md](architecture.md). The host
fixtures use synthetic identities; private hardware captures are kept outside Git.
