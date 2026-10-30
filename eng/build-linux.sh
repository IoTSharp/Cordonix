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
sources='src/cdx-core/result.c src/cdx-core/filesystem.c src/cdx-core/filesystem_posix.c src/cdx-core/process_posix.c src/cdx-workspace/document_common.c src/cdx-workspace/document_posix.c src/cdx-workspace/encoding_posix.c src/cdx-workspace/create_file.c src/cdx-workspace/create_file_posix.c src/cdx-protocol/json.c src/cdx-protocol/operations.c src/cdx-protocol/adapters.c src/cdx-protocol/tools.c src/cdx-cli/main_posix.c'

for source in $sources; do
    name=$(basename "$source" .c)
    timeout --kill-after=2s 30s "$cc" -std=c11 -D_POSIX_C_SOURCE=200809L "$version_define" "$revision_define" "-DCDX_BUILD_DIRTY=$dirty" -Iinclude -Wall -Wextra -Werror -pedantic -O2 -c "$root/$source" -o "$obj/$name.o"
done

timeout --kill-after=2s 30s "$cc" -std=c11 -O2 -o "$out/cdx" "$obj/result.o" "$obj/filesystem.o" "$obj/filesystem_posix.o" "$obj/process_posix.o" "$obj/document_common.o" "$obj/document_posix.o" "$obj/encoding_posix.o" "$obj/create_file.o" "$obj/create_file_posix.o" "$obj/json.o" "$obj/operations.o" "$obj/adapters.o" "$obj/tools.o" "$obj/main_posix.o" -pthread -lm
"$out/cdx" --version
