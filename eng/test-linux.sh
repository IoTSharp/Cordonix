#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
timeout --kill-after=3s 120s sh "$root/eng/build-linux.sh"
tests="$root/build/linux/tests"
mkdir -p "$tests"
cc=${CC:-gcc}
cflags='-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -pedantic -O2 -Iinclude'
timeout --kill-after=2s 30s "$cc" $cflags tests/process/process_fixture_posix.c -o "$tests/cdx-process-fixture"
timeout --kill-after=2s 30s "$cc" $cflags tests/process/test_process_posix.c src/cdx-core/process_posix.c src/cdx-core/result.c -pthread -o "$tests/cdx-process-tests"
timeout --kill-after=2s 30s "$cc" $cflags tests/protocol/test_json.c src/cdx-protocol/json.c -lm -o "$tests/cdx-json-tests"
timeout --kill-after=2s 30s "$cc" $cflags tests/protocol/test_stdio.c src/cdx-protocol/json.c -lm -o "$tests/cdx-stdio-tests"
timeout --kill-after=2s 30s "$cc" $cflags tests/workspace/test_document_posix.c src/cdx-workspace/document_common.c src/cdx-workspace/document_posix.c src/cdx-workspace/encoding_posix.c -o "$tests/cdx-document-tests"
cd "$tests"
timeout --kill-after=3s 60s ./cdx-process-tests "$tests/cdx-process-fixture" --pilot
CDX_PILOT=1 timeout --kill-after=3s 30s ./cdx-document-tests "$tests"
timeout --kill-after=3s 15s ./cdx-json-tests --pilot
timeout --kill-after=3s 15s ./cdx-stdio-tests "$root/build/linux/bin/cdx" "$tests/cdx-process-fixture" --pilot
timeout --kill-after=3s 60s ./cdx-process-tests "$tests/cdx-process-fixture"
timeout --kill-after=3s 60s ./cdx-document-tests "$tests"
timeout --kill-after=3s 15s ./cdx-json-tests
timeout --kill-after=3s 75s ./cdx-stdio-tests "$root/build/linux/bin/cdx" "$tests/cdx-process-fixture"
