#!/usr/bin/env bash
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-clang++}"
export ARDUINOJSON_INCLUDE="${ARDUINOJSON_INCLUDE:-$HOME/Documents/Arduino/libraries/ArduinoJson/src}"
if [[ ! -f "$ARDUINOJSON_INCLUDE/ArduinoJson.h" ]]; then
  echo 'Set ARDUINOJSON_INCLUDE to the ArduinoJson 7.4.3 src directory.' >&2
  exit 2
fi
if [[ "$(uname -s)" != Darwin ]]; then
  echo 'The storage fixture currently uses macOS CommonCrypto.' >&2
  exit 2
fi
mkdir -p "$REPO_ROOT/.build/tests"
FLAGS=(-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
for name in account_paging badge_styles button_gesture orientation_filter profile_urls; do
  "$CXX" "${FLAGS[@]}" "$REPO_ROOT/tests/check_$name.cpp" -o "$REPO_ROOT/.build/tests/$name"
  "$REPO_ROOT/.build/tests/$name"
done
"$CXX" "${FLAGS[@]}" -Wno-deprecated-declarations \
  -I"$REPO_ROOT/tests/profile-store-test/include" \
  "$REPO_ROOT/tests/profile-store-test/test.cpp" -o "$REPO_ROOT/.build/tests/profile-store"
"$REPO_ROOT/.build/tests/profile-store"
"$CXX" "${FLAGS[@]}" -pthread \
  -I"$REPO_ROOT/tests/background-http-host" -I"$ARDUINOJSON_INCLUDE" \
  "$REPO_ROOT/tests/background-http-host/check.cpp" -o "$REPO_ROOT/.build/tests/background-http"
"$REPO_ROOT/.build/tests/background-http"
export CXX
python3 "$REPO_ROOT/tests/profile-scheduler-check/run.py"
