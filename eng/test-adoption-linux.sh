#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec timeout --kill-after=10s 720s python3 "$root/tests/acceptance/linux_adoption.py" "$@"
