# Voice replies and the Devices gateway

The board owns the interaction. The dedicated `devices.chan.dev` gateway owns
the limited device operations. The separate `auth.chan.dev` service owns shared
identity and personal Pipes connections. This follows the existing Social → Auth
private service-binding pattern; Social is a reference consumer, not the home of
the device product. No new WorkOS application or duplicate connections are needed.

## Interaction

Open **Settings → X replies**. The inbox contains at most five eligible mentions
for the connected X account. Previous/Next selects a mention; Next page shows its
complete text. Hold blue for 250 ms to begin recording; release to finish and
transcribe. The recording is capped at 30 seconds. A short blue press does not
change the selected mention. Yellow has no reply-mode action, and the two-button
chord cancels local recording and opens Settings.

The transcript is paginated without dropping its body text. The existing fonts
are ASCII-only, so unsupported characters appear as explicit `[U+XXXX]` labels;
the original UTF-8 is retained for posting. Send becomes available only after all
pages have been displayed, with a valid live session and a selected recipient.
A touch begun before Send appeared cannot activate it on release. Re-record
returns to the recording-ready view; it does not immediately start the microphone.

Recording, transcript review, and sending hold the current screen orientation.
The badge's calibrated rotation, account paging, completed-tap handling, and
single `init()` layout per account are retained outside this app.

When X is unavailable but a speech connection is available, the app permits
dictation without a recipient. Its transcript cannot be sent as a reply. xAI is
the primary transcription provider. Deepgram fallback is deliberate: after a
primary failure, choose it and make a new recording. The firmware does not retain
or silently resubmit a failed clip.

## Service boundaries

Existing badge/workspace endpoints stay at `auth.chan.dev`. The voice transport
permits only the exact configured HTTPS routes below on `devices.chan.dev`.
Certificate validation and redirect rejection remain enabled.

| Route | Purpose |
| --- | --- |
| `GET /v1/x/replies` | Bounded mentions, verified owner/workspace, intended X sender, speech readiness |
| `POST /v1/transcriptions` | xAI transcription of one bounded canonical WAV |
| `POST /v1/transcriptions/deepgram` | Explicit Deepgram transcription |
| `POST /v1/x/replies` | Reviewed text, target, expected sender/connection, `Idempotency-Key` header |
| `GET /v1/x/replies/status` | Existing receipt lookup with the same key header; never a repost |

The gateway calls `chan-identity-web`'s private `DevicesIdentity` entrypoint via
`DEVICES_IDENTITY`. Auth verifies the Production Devices token and personal
workspace; callers cannot choose another user/workspace. Its provider-access
capability accepts only approved operations. Provider credentials reach the
gateway over this private binding and never enter public responses or firmware.
The gateway does not hold a WorkOS environment API key or browser cookie secret.

`gateway/` contains the Worker, durable send ledger, bounded provider adapters,
configuration, and runtime tests. Its own README is the operational reference.
Shared Auth is independently deployed from its repository.

## Recording and request ownership

`voice_recorder.h` uses a lifetime worker and M5Unified's StopWatch microphone
configuration: ES8311 on internal I²C, I2S1 with MCLK18/BCLK17/LRCK15/input16.
Speaker output remains disabled. Creating the worker does not start recording.

Audio is signed PCM16 little-endian, mono, 16 kHz, with a canonical 44-byte WAV
header. The maximum is 480,000 samples / **960,044 bytes**, held in one PSRAM
allocation. The worker queues 800-sample chunks and publishes the result only
after the library microphone task has joined. HTTP takes ownership without a
second audio copy and wipes it on completion or cancellation. No audio file is
saved on the board. Transcripts and inbox text are volatile.

The pinned M5Unified writer clips samples above `INT16_MIN`, allowing an impossible
sentinel to distinguish a queued chunk from a completed one. Review this assumption
when upgrading M5Unified. A faulty I2S driver can delay `Mic.end()` despite the
recorder's two-second chunk wait timeout. The UI remains responsive and the live
buffer stays owned until the writer exits; there is no forced task deletion.

## Sending and recovery

Before sending, the board saves one bounded NVS receipt record containing the
key, public client ID, owner, workspace, target, sender, and connection identity.
It does not persist the transcript. The gateway independently checks current
sender and target eligibility and durably records the attempt before posting.

Back, cancellation, loss of connectivity, or restart cannot retract a request
already dispatched. An unresolved receipt blocks another send; status checks use
the original key. `pending`, `unknown`, and `not_found` do not authorize a fresh
key or an automatic retry. Only a matching definitive `sent` or `failed` receipt
unlocks the flow. Another sign-in cannot use or dismiss the old owner's receipt.
Corrupt receipt storage fails closed and requires explicit recovery.

The durable gateway ledger also guards sender/target pairs against a fresh-key
retry after an uncertain result. Keep this guard during future schema changes;
expiring it into a resend path is unsafe. Provider-side network uncertainty is
reported honestly, not converted into a definitive failure.

## Verification status

Installed and checked September 11, 2026 (Pacific):

- Gateway `chan-devices` deployed at `devices.chan.dev`, version
  `8aa11087-8c5e-4bb2-88e7-dc62ec0cbf3e`. Shared Auth independently deployed
  `DevicesIdentity` in `chan-identity-web`, version
  `dbd48292-c511-43a9-a83e-caf34c79295d`.
- Complete firmware built to **1,568,231 application bytes** with **53,568
  static/global bytes** in the unchanged 3,145,728-byte application slot. Normal
  component upload verified hashes and preserved the session, Wi-Fi and cache.
- All firmware host checks passed with ASan/UBSan, including production-header
  controller/recorder tests. All **28 gateway Workers runtime tests** passed,
  including real Durable Object eviction and concurrent duplicate attempts.
  Independent review found no unresolved blocker. Sixteen production signed-out
  checks passed, including TLS 1.2 using the firmware's exact trust bundle.
- The physical microphone captured **30,400 samples / 1,900 ms** in a short
  local test. Cancellation stopped capture and returned to the badge with no
  uploaded result. Free PSRAM returned to the exact **6,918,640-byte** baseline.
  The measured main-loop maximum across that cancellation/return was 100 ms,
  including badge rendering; this is not a physical tap-latency measurement.
- A known phrase played by the workstation's speech synthesizer was captured by
  the board's real microphone: **76,000 samples / 4,750 ms / 152,044 WAV bytes**.
  The existing Production Devices bearer reached shared Auth through the gateway,
  and the user's personal **xAI** connection returned the complete expected
  phrase. The device's comparison passed and its review screen showed Send
  disabled without a recipient. This is a speaker-to-board transcription check,
  not a human voice quality survey. Deepgram fallback was tested in the Workers
  runtime, not against the live provider.
- The badge regression passed with three exact online/offline image matches and
  **18 QR decodes** across normal, expanded and circularly cropped frames.
  Cache startup measured **1,259 ms offline / 1,299 ms normal**; reconnect made
  zero avatar downloads and zero cache rewrites. Account paging, reserved yellow,
  and both-pusher Settings dispatch passed through the diagnostic controls.

**Remaining live X requirements:** the gateway returned the explicit X API
credits-required error, so live mentions and posting have not been verified.
Production's requested scopes now include `tweet.write`, but the existing X
connection still needs user reauthorization at
[Connections](https://auth.chan.dev/connections). No credits were purchased and
no public reply was posted. The installed app remains useful for dictation and
review while these X requirements are unresolved.

Private captures, source/component hashes and sanitized measurements are under
`.build/voice-verification/`, `.build/voice-badge-verification-after-restart/` and
`.build/gateway-preflight/` on the development workstation. A first badge run
overlapped a background request; the clean-restart run above passed. The newly
created domain initially hit negative DNS caches; a fresh board boot resolved
it. Production TLS checks used public DNS when the Mac's system resolver still
had the old negative result. None of these artifacts belongs in public Git.

Real reply tests require user-approved content and target or their deliberate
Send on the device. Do not turn a transcription test into a public post or delete
unresolved receipts to make a test pass.
