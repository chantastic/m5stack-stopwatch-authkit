# Voice recorder host check

This compiles the production `voice_recorder.h` with fake M5 microphone, PSRAM,
and FreeRTOS services. It checks WAV fields and sample bounds, no recording on
worker startup, allocation/task/microphone failures, delayed microphone startup,
stop, cancellation with a live writer, cancellation of a completed recording,
timeout, and the full 480,000-sample cap. Fake hardware deliberately runs faster
than real time. These are ownership and format checks, not microphone quality,
physical duration, I2S failure recovery, or measured device responsiveness.

From the repository root:

```sh
mkdir -p .build/tests
clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -pthread -Itests/voice-recorder-host tests/voice-recorder-host/check.cpp \
  -o .build/tests/voice-recorder
.build/tests/voice-recorder
```

The production recorder relies on M5Unified 0.2.19's signed-sample clipping
range to recognize a completed chunk even when `isRecording()` initially
reports false after enqueue. Keep that dependency explicit when upgrading.

The recorder's chunk wait has a two-second deadline, but safe shutdown still
joins the library's microphone task. Its inner sample loop ignores zero-byte
I2S reads and does not observe the stop flag between samples; repeated 100 ms
read timeouts can stretch one 800-sample chunk to roughly 40 seconds. A driver
call that never returns can hold shutdown indefinitely. This check deliberately
holds a writer during cancellation and verifies that main-task methods still
return immediately and audio remains allocated until that writer exits. It does
not promise bounded microphone shutdown under a physical I2S fault.
