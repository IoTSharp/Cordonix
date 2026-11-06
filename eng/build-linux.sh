#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
out="$root/build/linux/bin"
obj="$root/build/linux/obj"
mkdir -p "$out" "$obj"

cc=${CC:-gcc}
version=${CDX_VERSION:-0.1.0-dev}
case "$version" in
    ''|*[!0-9A-Za-z.+_-]*) echo "Invalid CDX_VERSION: $version" >&2; exit 2 ;;
esac
version_define="-DCDX_VERSION=\"$version\""
revision=$(timeout --kill-after=2s 10s git rev-parse --verify HEAD)
changes=$(timeout --kill-after=2s 10s git status --porcelain=v1 --untracked-files=normal)
dirty=0
if [ -n "$changes" ]; then dirty=1; fi
revision_define="-DCDX_BUILD_REVISION=\"$revision\""
sources='src/cdx-core/result.c src/cdx-core/filesystem.c src/cdx-core/filesystem_posix.c src/cdx-core/file_stat.c src/cdx-core/file_stat_posix.c src/cdx-core/process_posix.c src/cdx-core/process_query_posix.c src/cdx-core/query_processes_posix.c src/cdx-core/phone.c src/cdx-core/phone_protocol.c src/cdx-core/phone_posix.c src/cdx-core/ssh_posix.c src/cdx-core/proxy_posix.c src/cdx-core/http_posix.c src/cdx-core/serial_posix.c src/cdx-core/network_posix.c src/cdx-core/screenshot_posix.c src/cdx-workspace/document_common.c src/cdx-workspace/document_posix.c src/cdx-workspace/encoding_posix.c src/cdx-workspace/create_file.c src/cdx-workspace/create_file_posix.c src/cdx-protocol/json.c src/cdx-protocol/presentation.c src/cdx-protocol/operations.c src/cdx-protocol/data_query.c src/cdx-protocol/discovery.c src/cdx-protocol/program_discovery.c src/cdx-protocol/file_operations.c src/cdx-core/file_ops.c src/cdx-core/file_ops_posix.c src/cdx-protocol/adapters.c src/cdx-protocol/process_query.c src/cdx-protocol/ssh.c src/cdx-protocol/http.c src/cdx-protocol/serial.c src/cdx-protocol/network.c src/cdx-protocol/screenshot.c src/cdx-protocol/tools.c src/cdx-cli/main_posix.c'

for source in $sources; do
    name=$(basename "$source" .c)
    timeout --kill-after=2s 30s "$cc" -std=c11 -D_POSIX_C_SOURCE=200809L "$version_define" "$revision_define" "-DCDX_BUILD_DIRTY=$dirty" -Iinclude -Wall -Wextra -Werror -pedantic -O2 -c "$root/$source" -o "$obj/$name.o"
done

timeout --kill-after=2s 30s "$cc" -std=c11 -O2 -o "$out/cdx" "$obj/result.o" "$obj/filesystem.o" "$obj/filesystem_posix.o" "$obj/file_stat.o" "$obj/file_stat_posix.o" "$obj/process_posix.o" "$obj/process_query_posix.o" "$obj/query_processes_posix.o" "$obj/phone.o" "$obj/phone_protocol.o" "$obj/phone_posix.o" "$obj/ssh_posix.o" "$obj/proxy_posix.o" "$obj/http_posix.o" "$obj/serial_posix.o" "$obj/network_posix.o" "$obj/screenshot_posix.o" "$obj/document_common.o" "$obj/document_posix.o" "$obj/encoding_posix.o" "$obj/create_file.o" "$obj/create_file_posix.o" "$obj/json.o" "$obj/presentation.o" "$obj/operations.o" "$obj/data_query.o" "$obj/discovery.o" "$obj/program_discovery.o" "$obj/file_operations.o" "$obj/file_ops.o" "$obj/file_ops_posix.o" "$obj/adapters.o" "$obj/process_query.o" "$obj/ssh.o" "$obj/http.o" "$obj/serial.o" "$obj/network.o" "$obj/screenshot.o" "$obj/tools.o" "$obj/main_posix.o" -pthread -lm
"$out/cdx" --version
