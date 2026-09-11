This fixture runs the production `firmware/devices_badge/profile_store.h` against
a temporary local filesystem, fake NVS/partition/mount surfaces, and injected IO
failures. SHA-256 uses macOS CommonCrypto. It never opens a serial port or uses
network access.

Run `./scripts/test.sh` from the repository root. The runner builds this and the
other host fixtures under the ignored `.build/` directory with AddressSanitizer
and UndefinedBehaviorSanitizer. The hashing stub requires macOS CommonCrypto.

The production LittleFS implementation supplies atomic rename and flash wear
leveling; this host fixture checks the caller's transaction and invalidation
ordering, error handling, wire format, and ownership rules. This fixture does not
simulate physical flash power loss or measure startup timing.
