#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
timeout --kill-after=3s 120s sh "$root/eng/build-linux.sh"
tests="$root/build/linux/tests"
mkdir -p "$tests"
cc=${CC:-gcc}
version=${CDX_VERSION:-0.1.0-dev}
version_define="-DCDX_VERSION=\"$version\""
cflags='-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -pedantic -O2 -Iinclude'
timeout --kill-after=2s 30s "$cc" $cflags "$version_define" tests/process/process_fixture_posix.c -o "$tests/cdx-process-fixture"
timeout --kill-after=2s 30s "$cc" $cflags "$version_define" tests/process/test_process_posix.c src/cdx-core/process_posix.c src/cdx-core/result.c -pthread -o "$tests/cdx-process-tests"
timeout --kill-after=2s 30s "$cc" $cflags "$version_define" tests/protocol/test_json.c src/cdx-protocol/json.c -lm -o "$tests/cdx-json-tests"
timeout --kill-after=2s 30s "$cc" $cflags "$version_define" tests/protocol/test_stdio.c src/cdx-protocol/json.c -lm -o "$tests/cdx-stdio-tests"
timeout --kill-after=2s 30s "$cc" $cflags "$version_define" tests/protocol/test_adapters.c src/cdx-protocol/adapters.c src/cdx-protocol/json.c src/cdx-core/process_posix.c src/cdx-core/result.c src/cdx-core/filesystem.c src/cdx-core/filesystem_posix.c src/cdx-workspace/encoding_posix.c -pthread -lm -o "$tests/cdx-adapter-tests"
timeout --kill-after=2s 30s "$cc" $cflags "$version_define" tests/workspace/test_document_posix.c src/cdx-workspace/document_common.c src/cdx-workspace/document_posix.c src/cdx-workspace/encoding_posix.c -o "$tests/cdx-document-tests"
timeout --kill-after=2s 30s "$cc" $cflags "$version_define" tests/workspace/test_create_file.c src/cdx-workspace/create_file.c src/cdx-workspace/create_file_posix.c src/cdx-workspace/document_common.c src/cdx-workspace/document_posix.c src/cdx-workspace/encoding_posix.c src/cdx-core/filesystem.c src/cdx-core/filesystem_posix.c -o "$tests/cdx-create-file-tests"
cd "$tests"
timeout --kill-after=3s 60s ./cdx-process-tests "$tests/cdx-process-fixture" --pilot
CDX_PILOT=1 timeout --kill-after=3s 30s ./cdx-document-tests "$tests"
timeout --kill-after=3s 30s ./cdx-create-file-tests "$tests" --pilot
timeout --kill-after=3s 15s ./cdx-json-tests --pilot
timeout --kill-after=3s 15s ./cdx-stdio-tests "$root/build/linux/bin/cdx" "$tests/cdx-process-fixture" --pilot
timeout --kill-after=3s 60s ./cdx-process-tests "$tests/cdx-process-fixture"
timeout --kill-after=3s 60s ./cdx-document-tests "$tests"
timeout --kill-after=3s 30s ./cdx-create-file-tests "$tests"
timeout --kill-after=3s 15s ./cdx-json-tests
timeout --kill-after=3s 75s ./cdx-stdio-tests "$root/build/linux/bin/cdx" "$tests/cdx-process-fixture"
if command -v rg >/dev/null 2>&1; then
    timeout --kill-after=5s 85s sh "$root/eng/test-adapters-linux.sh"
else
    echo 'ripgrep adapter NOT_RUN: this environment has no rg; validating Git only' >&2
    timeout --kill-after=5s 85s sh "$root/eng/test-adapters-linux.sh" --git-only
fi
