#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  printf 'usage: %s <layout-fix-x11-test-binary>\n' "$0" >&2
  exit 2
fi

test_bin="$1"

exec xvfb-run -a -s "-screen 0 640x480x24 -noreset" \
  sh -eu -c '
    setxkbmap -rules evdev -model pc105 -layout us,ua,ru
    exec "$1" --isolated-display
  ' sh "$test_bin"

