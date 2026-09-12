#!/usr/bin/env bash
# Build and upload Clockulator to the ESP32-S3-LCD-1.3.
#
# Board settings live in Clockulator/sketch.yaml, so arduino-cli picks them up
# on its own. This script only locates arduino-cli and the serial port.
#
# Usage: tools/flash.sh [port]        (or set CLOCKULATOR_PORT)
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
sketch="$here/../Clockulator"

cli="$(command -v arduino-cli || true)"
if [[ -z "$cli" ]]; then
  # Arduino IDE 2.x bundles a copy that is not put on PATH.
  bundled="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
  [[ -x "$bundled" ]] && cli="$bundled"
fi
if [[ -z "$cli" ]]; then
  echo "arduino-cli not found: install it, or install Arduino IDE 2.x" >&2
  exit 1
fi

port="${1:-${CLOCKULATOR_PORT:-}}"
if [[ -z "$port" ]]; then
  # The board shows up twice. The CH340 bridge (wchusbserial/usbserial/ttyUSB)
  # carries both uploads and Serial output. The S3's native USB
  # (usbmodem/ttyACM) also appears but Serial is not routed there with these
  # board settings, so prefer the bridge.
  for p in /dev/cu.wchusbserial* /dev/cu.usbserial* /dev/ttyUSB* /dev/cu.usbmodem* /dev/ttyACM*; do
    if [[ -e "$p" ]]; then port="$p"; break; fi
  done
fi
if [[ -z "$port" ]]; then
  echo "no serial port found: pass one, or set CLOCKULATOR_PORT" >&2
  exit 1
fi

echo "port: $port"
"$cli" compile "$sketch"
"$cli" upload -p "$port" "$sketch"
