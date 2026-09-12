#!/usr/bin/env python3
"""Compile the actual voice controller with deterministic device/service fakes."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
output = root / ".build" / "tests" / "voice-controller"
output.mkdir(parents=True, exist_ok=True)
arduinojson = Path(os.environ.get("ARDUINOJSON_INCLUDE", str(Path.home() / "Documents/Arduino/libraries/ArduinoJson/src")))
command = [os.environ.get("CXX", "clang++"), "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
           "-fno-omit-frame-pointer", "-Wno-deprecated-declarations", "-I" + str(Path(__file__).parent),
           "-I" + str(arduinojson), str(Path(__file__).with_name("check.cpp")), "-o", str(output / "check")]
subprocess.run(command, check=True)
result = subprocess.run([str(output / "check")], text=True, capture_output=True)
if result.returncode:
    print(result.stdout, end="")
    print(result.stderr, end="")
    result.check_returncode()
print(result.stdout, end="")
(output / "verification.txt").write_text(result.stdout)
sources = ["firmware/devices_badge/voice_reply.h", "firmware/devices_badge/voice_reply_state.h",
           "firmware/devices_badge/voice_reply_ui.h", "tests/voice-controller-host/check.cpp",
           "tests/voice-controller-host/Arduino.h", "tests/voice-controller-host/run.py"]
(output / "source-manifest.json").write_text(json.dumps({name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sources}, indent=2) + "\n")
