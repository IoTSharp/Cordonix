#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
tests="$root/build/linux/capability-gaps"
mkdir -p "$tests"
cc=${CC:-gcc}
cflags='-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -pedantic -O2 -Iinclude'
timeout --kill-after=2s 30s "$cc" $cflags tests/protocol/test_data_query.c src/cdx-protocol/data_query.c src/cdx-protocol/json.c src/cdx-workspace/document_common.c src/cdx-workspace/document_posix.c src/cdx-workspace/encoding_posix.c src/cdx-core/filesystem.c src/cdx-core/filesystem_posix.c -lm -o "$tests/cdx-data-query-tests"
timeout --kill-after=2s 30s "$tests/cdx-data-query-tests" "$tests"
timeout --kill-after=2s 30s "$cc" $cflags tests/workspace/test_file_stat.c src/cdx-core/file_stat.c src/cdx-core/file_stat_posix.c src/cdx-core/filesystem.c src/cdx-core/filesystem_posix.c src/cdx-workspace/encoding_posix.c -o "$tests/cdx-file-stat-tests"
timeout --kill-after=2s 30s "$tests/cdx-file-stat-tests" "$tests"
timeout --kill-after=2s 30s "$cc" $cflags tests/protocol/test_program_discovery.c src/cdx-protocol/program_discovery.c src/cdx-protocol/json.c src/cdx-core/filesystem.c src/cdx-core/filesystem_posix.c src/cdx-workspace/encoding_posix.c -lm -o "$tests/cdx-program-discovery-tests"
timeout --kill-after=2s 30s "$tests/cdx-program-discovery-tests"
timeout --kill-after=2s 30s "$cc" $cflags tests/workspace/test_file_ops.c src/cdx-core/file_ops.c src/cdx-core/file_ops_posix.c src/cdx-protocol/file_operations.c src/cdx-protocol/json.c src/cdx-core/filesystem.c src/cdx-core/filesystem_posix.c src/cdx-workspace/encoding_posix.c -lm -o "$tests/cdx-file-ops-tests"
timeout --kill-after=2s 30s "$tests/cdx-file-ops-tests" "$tests"
timeout --kill-after=2s 30s "$cc" $cflags tests/protocol/test_discovery.c src/cdx-protocol/discovery.c src/cdx-protocol/json.c src/cdx-core/process_posix.c src/cdx-core/result.c src/cdx-core/filesystem.c src/cdx-core/filesystem_posix.c src/cdx-workspace/encoding_posix.c -pthread -lm -o "$tests/cdx-discovery-tests"
rg_program=${CDX_RG:-$(command -v rg || true)}
if [ -n "$rg_program" ]; then
    timeout --kill-after=2s 30s "$tests/cdx-discovery-tests" "$rg_program" "$tests"
else
    echo 'find_files real-rg tests NOT_RUN: native Linux rg is absent.' >&2
fi
