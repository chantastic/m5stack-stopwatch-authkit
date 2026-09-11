# Privacy and local storage

This firmware is configured for chan.dev's Production Devices application. Each signed-in user sees their own connected profiles. The repository contains source and public configuration; device credentials and personal test captures are kept outside it.

## Where data lives

| Data | Location |
| --- | --- |
| Public AuthKit client ID, service URLs, certificate roots | Firmware source and compiled application |
| Wi-Fi network name and password | Device NVS, entered through its local setup portal |
| WorkOS refresh token, user ID, organization ID, and account email | Device NVS session record |
| WorkOS access token | Device RAM |
| LinkedIn, X, and GitHub OAuth/provider tokens | Existing chan.dev/WorkOS services; not sent to this firmware |
| Public profile name, handle, IDs, URLs, and avatar pixels | Device RAM and LittleFS cache |
| Selected account and styles | Device NVS |

The public-profile cache excludes email, Wi-Fi credentials, access/refresh tokens, and raw authentication responses. Its owner/workspace identifiers are needed to keep users' records separate. The cached portrait comes from the user's profile and is processed on the device; it is not embedded as a personal image in the source.

## Wi-Fi setup and network requests

Wi-Fi setup creates a temporary hotspot protected by a newly generated password shown in its QR code. The local portal uses HTTP at `192.168.4.1` over that hotspot. It accepts setup requests from the hotspot interface, checks a per-session nonce, and saves credentials only after joining the selected network. The hotspot closes after success, when leaving setup, or after ten minutes.

WorkOS, backend, and avatar traffic use certificate-validated HTTPS. The firmware sends the WorkOS bearer token only to the exact configured profile/workspace endpoints. Avatar requests use restricted provider CDN URLs, omit the bearer token, and do not follow redirects. Downloads and decoding have size limits.

The device's session checks validate context on a trusted HTTPS response; they are not a general-purpose offline JWT verifier. Authorization for provider data is enforced by the separately maintained backend.

## Offline behavior

A saved badge represents the last successfully saved public data. It can be shown without Wi-Fi or a current access token; Settings labels this state accordingly. The cache never creates an authenticated session.

A detected user/workspace change or explicit authentication rejection invalidates cached profiles. Provider disconnection invalidates the affected provider. If the linked remote account changes, its former identity is invalidated before its replacement is saved. Temporary network failures retain matching saved data for display. A sign-out or disconnection made elsewhere cannot be discovered while the device is offline.

Cache hashes and context checks protect against damaged files and accidental account mixups. They do not make local storage confidential or prevent a person with physical flash access from changing it.

## Physical device and publishing

**Device storage is currently unencrypted.** A physical flash read or full-device backup can contain Wi-Fi credentials, the refresh token, account email, identifiers, and cached profiles. This personal prototype does not enable secure boot, flash encryption, or change security eFuses.

Keep full-device backups private. Distribute reviewed source or source-built application components, never a dump taken from a provisioned board. The normal flash script replaces application components while preserving existing NVS and file storage; it is not a credential-erasure tool.

USB diagnostics do not expose passwords or tokens, but some report public pairing information, client/user/session identifiers, connection state, and device metrics. Display captures can contain names, portraits, and public profile QR codes. Review diagnostic output before publishing it. `BADGE_STORE` counters contain storage/activity metrics rather than cached profile contents.
