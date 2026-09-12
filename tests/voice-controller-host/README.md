# Voice controller host checks

Run `python3 tests/voice-controller-host/run.py` from the repository root. The
runner compiles the actual `voice_reply.h`, `voice_reply_state.h`, and
`voice_reply_ui.h` with AddressSanitizer and UndefinedBehaviorSanitizer. It writes
the executable, successful result, and source hashes under
`.build/tests/voice-controller/`.

The fixture replaces hardware, Preferences, microphone completion, and HTTP with
deterministic fakes. It makes no network calls and never publishes an X reply.
Separate transport and recorder fixtures verify those production workers.

These checks exercise:

- The receipt write and readback happen before a send is enqueued. Storage
  failure prevents submission; a locally rejected enqueue never retries itself.
- JSON escaping is included in the request limit before recording an intent.
- Every transcript page must be rendered before Send becomes available. A
  contact begun before Review, or invalidated by a transition, cannot Send.
- The verified inbox's boolean posting permission gates Send independently of
  recording availability, the transcription's boolean reply validation gates
  submission without hiding review text, and connectivity changes update UI
  capabilities.
- Authentication, workspace, or network loss cancels recording. A completed
  30-second recording uploads once even while blue remains held; release does
  not send the transcription.
- Back and the Settings chord discard capture, preserve an enqueued send's
  receipt, and clear a definitely unsubmitted intent. Restart can reconcile a
  receipt with GET; unknown, missing, or mismatched results cannot cause repost.
- Explicit Deepgram fallback needs another recording. An unverified response
  cannot supply review text.
- Bounded live-record diagnostics use the normal flow; the local mic check
  never uploads. Transcript verification outputs comparison metrics only, and
  there is no diagnostic Send action.

These are controller behavior tests. They do not measure microphone quality,
physical gestures, display readability, flash power-loss recovery, provider
availability, or real network timing. Hardware verification is recorded
separately and remains private.
