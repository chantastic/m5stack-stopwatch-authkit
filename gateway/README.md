# Devices gateway

This Worker serves the StopWatch voice reply app at `https://devices.chan.dev`.
It belongs to this device project. Auth remains the shared identity service:
there is no new WorkOS application, no copied AuthKit implementation, and no
migration of the existing badge/profile endpoints.

The gateway calls the private service binding `DEVICES_IDENTITY` →
`chan-identity-web` / `DevicesIdentity`. Auth verifies the **Production Devices**
bearer session, fresh session revocation and personal workspace, and vends one
operation-scoped provider credential. The gateway owns mentions, audio handling,
reply policy, quotas, and durable send receipts. It has no WorkOS SDK, WorkOS
secret, provider key in configuration, cookie login, public credential endpoint,
or arbitrary provider proxy.

## Development

From this directory, using Node.js 22 or newer:

```sh
npm ci
npm run check
npm test
npm run dry-run
```

`check` regenerates `worker-configuration.d.ts` from Wrangler and runs TypeScript.
The generated binding type is narrowed by a local declaration of the external
RPC contract in `src/auth.ts`; every returned field is independently validated.
Tests execute the real Worker, private RPC calls to a separate fake Auth Worker,
and SQLite Durable Objects in the Workers runtime. Provider fetches are explicitly
intercepted, with no fallback to the network. Test identities/audio are synthetic.

The toolchain is exact-pinned in `package.json` and its lockfile. The current
Cloudflare Vitest plugin supports Vitest 4.1, so this project uses **4.1.11**
instead of an incompatible Vitest major. The `sharp` override selects the patched
**0.35.4** used by local runtime tooling; it is not a production audio dependency.

Wrangler's dry-run output goes to the repository's ignored `.build/gateway/`.
Do not commit runtime output, audio, device captures, provider responses, or
private identity logs. `node_modules`, `.dev.vars`, and local Worker state are
ignored. Normal tests require no credentials or live provider traffic.

Deployment is an explicit operation:

```sh
npm run deploy
```

`wrangler.jsonc` names `chan-devices`, uses a custom domain, disables workers.dev
and preview URLs, and creates the SQLite-backed `ReplyLedger` Durable Object with
the `v1` migration. The private Auth entrypoint must be deployed first. This is
one intentionally configured Production deployment, not an implicit staging
service. Review the account/domain before running deployment; the firmware
continues to use `auth.chan.dev` for its existing badge APIs.

## Public HTTP contract

Every `/v1/` route requires the existing `Authorization: Bearer <Devices token>`.
Cookies are ignored. Methods and paths are exact; query strings are rejected.
All responses are JSON, at most **8,192 UTF-8 bytes**, with `private, no-store`.
`GET /health` is a public service-health response and does not test identity or
provider connections.

| Route | Input | Result |
| --- | --- | --- |
| `GET /v1/x/replies` | Devices bearer | At most five complete eligible mentions, sender, verified context and transcription readiness |
| `POST /v1/transcriptions` | `Content-Type: audio/wav`, raw canonical WAV | xAI transcription; no automatic fallback |
| `POST /v1/transcriptions/deepgram` | Same WAV format | An explicitly selected Deepgram attempt |
| `POST /v1/x/replies` | JSON described below, plus `Idempotency-Key` header | One explicit reply attempt or its existing receipt |
| `GET /v1/x/replies/status` | The same `Idempotency-Key` header | Existing authenticated receipt; never posts to X |

A successful inbox response has this shape:

```json
{
  "state": "ready",
  "context": { "userId": "user_example", "organizationId": "org_example" },
  "sender": { "id": "123", "username": "example", "connectionId": "connection_example" },
  "items": [
    {
      "id": "456", "text": "@example A question for you.",
      "author": { "id": "789", "username": "another", "name": "Another Person" },
      "createdAt": "2026-09-11T12:00:00.000Z"
    }
  ],
  "transcription": { "xai": true, "deepgram": true },
  "canSend": true
}
```

`items: []` is an honest empty inbox. Each included mention is at most **1,000
UTF-8 bytes**, with a bounded author and date; longer or unsuitable items are
omitted whole rather than truncated into an apparently complete reply target.
Only posts by another account with a provider-supplied explicit `@mention`
entity matching the verified current sender are included. Inherited reply-chain
participants and arbitrary posts do not qualify. The gateway takes a stricter
subset of X's capabilities; X still enforces its final reply restrictions.

Readiness flags come from Auth's metadata-only context response. `false` means
not currently available; it can include a temporary provider/identity failure,
so it must not be presented as proof that a connection was deleted. If the X
read fails *after* context resolution, the error retains `context` and
`transcription`. This allows dictation/review without inventing an X target or
enabling Send. A connected X token without `tweet.write` returns `canSend: false`
and reconnect guidance.

The WAV body is exactly a **44-byte RIFF/WAVE PCM header** followed by signed
little-endian PCM16 samples: **16,000 Hz, mono, two-byte block alignment**, no
extra chunks or trailing bytes. Maximum duration is **30 seconds**, maximum
body **960,044 bytes**; at least one sample is required. The byte count, RIFF
length, format, rates, channel count, sample width, data label and data length
are all checked. Compressed request bodies are rejected.

Transcription success:

```json
{
  "state": "transcribed", "provider": "xai", "text": "The complete reply.",
  "durationMs": 1200, "replyValid": true, "weightedLength": 19,
  "context": { "userId": "user_example", "organizationId": "org_example" }
}
```

The returned transcript is at most **3,000 UTF-8 bytes** and is never truncated.
X reply validity uses the official `twitter-text` weighted 280-character rules,
including emoji and fixed-weight URLs. A longer transcription can be reviewed
but cannot be posted; the device must ask for a shorter recording. No automatic
rewriting or sending occurs. xAI uses `POST https://api.x.ai/v1/stt` with the WAV
file last in multipart form data. Deepgram uses its prerecorded `nova-3` route.
A timeout can occur after a transcription provider has processed/billed audio;
retrying or choosing Deepgram is a new deliberate attempt, not a silent fallback.

Send input is at most **4,096 UTF-8 bytes**, with no extra fields:

```json
{
  "targetId": "456", "text": "The exact reviewed reply.",
  "senderId": "123", "connectionId": "connection_example"
}
```

The header key is 16–80 ASCII letters, digits, `_` or `-`; a random UUID is
suitable. User/workspace identity is always derived from Auth, never supplied in
the body. The expected sender and connection come from the inbox. Immediately
before posting, the gateway obtains fresh `x:reply` access, checks `tweet.write`,
compares the connection, calls X `/2/users/me`, and retrieves the target again to
verify the explicit mention. Replacing a connection cannot silently switch the
account from which a reviewed reply is sent.

## Receipts, uncertainty, and storage

One Durable Object is named by the authenticated client, user and personal
workspace. It also stores an immutable owner record as a second routing check.

For an admitted, structurally valid send request it stores the key, fingerprint
of the exact request, sender, target, state and sanitized receipt. It reserves a
unique **sender + target** guard in the same SQLite transaction and calls
`storage.sync()` before any provider POST. Neither the bearer, provider token,
audio nor the transcript/reply body is stored. No personal bodies or credentials
are logged.

Receipt responses include `state`, `key`, `targetId`, `senderId`, verified public
`context`, and `retryable: false`. `sent` additionally includes
`reply: {id, url}`; failures include a stable `error` and user-facing `message`.

| State | Meaning and permitted behavior |
| --- | --- |
| `pending` (202) | A persisted attempt is preparing or awaiting X; check status |
| `sent` (200) | X returned a valid successful post ID; show confirmation |
| `failed` (200) | Preparation failed before POST, or X definitively rejected the POST; the device may allow a later, new explicit attempt |
| `unknown` (202) | A POST may have reached X; keep the guard and check status, never blindly resend |

A repeat of the same key and identical body returns its existing receipt. A
changed body gets `409 idempotency_conflict`, without changing the original
receipt. A fresh key cannot bypass a pending, unknown or sent sender/target
guard; it receives a persisted failed receipt. Definitive failures release only
their own target guard. Sent/unknown guards and their receipts never expire or
get pruned. An abandoned pending receipt becomes unknown after 90 seconds; time
passage never turns uncertainty into permission to send.

The gateway has a **50-second request budget**, with bounded identity, upload,
and provider waits. It checks the remaining budget before POST and does not
start a POST with less than five seconds remaining. Pre-dispatch expiration is
failed; timeout, transport failure, redirect, malformed success or 5xx after
POST is unknown. Provider request cancellation does not prove the provider did
not act. A late process/storage failure can leave pending data which later
becomes unknown. No background retry sends posts.

Status is scoped to the current authenticated owner/workspace. `404
receipt_not_found` does **not** prove no post happened and must not unlock a
persisted uncertain send on the device. Reauthorization, malformed requests,
identity outages, or failure before admission can leave a device's pending key
without a gateway receipt. This conservative case needs manual inspection;
there is intentionally no automatic “clear and resend” escape. Likewise an
unknown receipt cannot currently be reconciled automatically by inspecting X;
check the account manually. Do not remove server guards just to retry a test.

## Quotas and bounds

The following are per authenticated client/user/workspace, in fixed windows.
They are local product limits, separate from X/xAI/Deepgram billing and quotas.

| Action | Limit |
| --- | --- |
| Inbox refresh | 12 / minute |
| Transcription attempt, including explicit fallback | 4 / minute and 40 / UTC day |
| Admitted send attempts | 6 / minute |
| Status checks | 60 / minute |
| New retained send receipts, including definitive failures | 60 / hour; 10,000 retained total |

The daily audio maximum corresponds to at most **20 minutes** of uploaded audio;
invalid/denied attempts conservatively consume quota too. Existing receipt
lookup happens before new-receipt admission. Once the admission/hour or retained
capacity limit is reached, a new key gets a bounded 429 without creating another
row, while existing status remains available. That response is not a durable
failed receipt; a lost response therefore remains conservative on the device.
The retained-capacity limit requires an operator to review capacity; it must not
be fixed by deleting unknown/sent receipts or target guards. There are no billing
purchases or quota increases in this gateway.

## Verification and primary references

The initial production deployment on September 11, 2026 (Pacific) is
`8aa11087-8c5e-4bb2-88e7-dc62ec0cbf3e`. The existing board's Production Devices
session successfully resolved through the private shared Auth binding and
transcribed one 4.75-second physical recording with its personal xAI connection.
The complete expected phrase appeared on the board's review screen. Production
signed-out route and TLS checks passed. Live X returned its credits-required
error; mentions/posting still need X credits. The subsequent shared Auth consent
check verified the existing connection has all requested grants, including
`tweet.write`. No public reply or credit purchase was made.
See [the device verification record](../docs/voice-replies.md#verification-status)
for exact measurements and limitations.

September 11, 2026 local checks exercise the real Workers runtime and SQLite
storage with fake Auth/provider services. Coverage includes private identity and
workspace boundaries, session/connection/sender changes, weighted text and WAV
bounds, explicit fallback, response/header privacy, request cancellation,
concurrent sends, durable state observed at POST dispatch, real Durable Object
eviction, injected stale pending state, new-key guards, and admission quotas.
These runtime tests are separate from the production checks recorded above;
they do not simulate radio behavior or prove live X posting. Automated tests
must never post a real X reply.

- [Cloudflare Workers best practices](https://developers.cloudflare.com/workers/best-practices/workers-best-practices/)
- [SQLite Durable Object storage and transaction/sync semantics](https://developers.cloudflare.com/durable-objects/api/sqlite-storage-api/)
- [Cloudflare Workers Vitest configuration](https://developers.cloudflare.com/workers/testing/vitest-integration/configuration/)
- [WorkOS Pipes credentials](https://workos.com/docs/reference/pipes/access-token)
- [X mentions timeline](https://docs.x.com/x-api/posts/timelines/quickstart/user-mention-quickstart)
- [X create-post API](https://docs.x.com/x-api/posts/create-post)
- [Official twitter-text library](https://github.com/twitter/twitter-text)
- [xAI speech-to-text API](https://docs.x.ai/developers/model-capabilities/audio/speech-to-text)
- [Deepgram prerecorded audio](https://developers.deepgram.com/docs/pre-recorded-audio)
