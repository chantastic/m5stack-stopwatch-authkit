# Badge scheduler regressions

Run `./scripts/test.sh` from the repository root, or run `python3 tests/profile-scheduler-check/run.py` independently. It extracts the current authentication, profile scheduling, workspace selection, session acceptance, cache invalidation, cache restoration, profile refresh, and account activation functions, plus the saved-session restoration portion of setup. Generated function bodies are unchanged. `ARDUINOJSON_INCLUDE` can override the default `~/Documents/Arduino/libraries/ArduinoJson/src` include directory.

The fixture substitutes deterministic time, radio, HTTP completion, storage, and display edges. JWT claim decoding is mocked at its verified-response boundary. The tests check control flow and ownership; real TLS and hardware are validated separately.

The runner compiles with AddressSanitizer and UndefinedBehaviorSanitizer, runs the focused regressions, and writes generated code, `verification.txt`, and source/function hashes under the ignored `.build/profile-scheduler-check/` directory. All session values in the fixture are synthetic.

The cold-fill regression uses the real profile validation and account paging helpers: LinkedIn and X may finish before the saved GitHub selection without activating a fallback. GitHub activates when its metadata arrives. Avatar rendering remains mocked; these synthetic fixtures intentionally contain no avatar URL.
