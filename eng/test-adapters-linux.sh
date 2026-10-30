#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
git=$(command -v git) || { echo 'Git adapter NOT_RUN: git is missing' >&2; exit 77; }
rg=$(command -v rg || true)
mode=${1:-full}
case "$mode" in full|--git-only) ;; *) echo 'Expected full or --git-only' >&2; exit 2 ;; esac
if [ -z "$rg" ]; then
    if [ "$mode" != --git-only ]; then echo 'ripgrep adapter NOT_RUN: rg is missing' >&2; exit 77; fi
    rg=/not-installed/rg
fi
owned=$(mktemp -d /tmp/cordonix-adapters-XXXXXXXX)
case "$owned" in /tmp/cordonix-adapters-????????) ;; *) exit 2 ;; esac
trap 'rm -rf -- "$owned"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
mkdir "$owned/pilot" "$owned/full"
timeout --kill-after=3s 35s "$root/build/linux/tests/cdx-adapter-tests" "$git" "$rg" "$owned/pilot" --pilot
if [ "$mode" = --git-only ]; then
    timeout --kill-after=3s 45s "$root/build/linux/tests/cdx-adapter-tests" "$git" "$rg" "$owned/full" --git-only
else
    timeout --kill-after=3s 45s "$root/build/linux/tests/cdx-adapter-tests" "$git" "$rg" "$owned/full"
fi
