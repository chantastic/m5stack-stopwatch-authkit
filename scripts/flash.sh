#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 1 || -z "$1" ]]; then
  echo "Usage: $0 SERIAL_PORT" >&2
  exit 2
fi
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARDUINO_CLI="${ARDUINO_CLI:-arduino-cli}"
FQBN='esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,UploadSpeed=460800'
"$REPO_ROOT/scripts/build.sh"
"$ARDUINO_CLI" upload --fqbn "$FQBN" --port "$1" \
  --input-dir "$REPO_ROOT/.build/firmware" \
  "$REPO_ROOT/firmware/devices_badge"
