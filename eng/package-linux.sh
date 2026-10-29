#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

version=${1:-${CDX_VERSION:-0.1.0-dev}}
case "$version" in
    ''|*[!0-9A-Za-z.+_-]*) echo "Invalid release version: $version" >&2; exit 2 ;;
esac

binary="$root/build/linux/bin/cdx"
if [ ! -f "$binary" ] || [ ! -x "$binary" ]; then
    echo "Linux executable is missing or not executable: $binary" >&2
    exit 1
fi
reported_version=$(timeout --kill-after=2s 15s "$binary" --version)
if [ "$reported_version" != "Cordonix cdx $version" ]; then
    echo "Executable version '$reported_version' does not match '$version'." >&2
    exit 1
fi

output_dir=${OUTPUT_DIRECTORY:-"$root/build/packaging"}
mkdir -p "$output_dir"
case "$output_dir" in
    /*) ;;
    *) echo "OUTPUT_DIRECTORY must be absolute" >&2; exit 2 ;;
esac
task_root=$(mktemp -d "$output_dir/.package-linux.XXXXXX")
case "$task_root" in
    "$output_dir"/*) ;;
    *) echo "Temporary package directory escaped the output directory" >&2; exit 2 ;;
esac
staging="$task_root/staging"
mkdir -p "$staging/examples" "$staging/docs"
success=0
cleanup() {
    status=$?
    if [ "$success" -eq 1 ]; then
        rm -rf -- "$task_root"
    else
        echo "Package failed; task-owned diagnostics retained at $task_root" >&2
    fi
    trap - EXIT HUP INT TERM
    exit "$status"
}
trap cleanup EXIT HUP INT TERM

copy_file() {
    source=$1
    target=$2
    if [ ! -f "$source" ]; then
        echo "Required package input is missing: $source" >&2
        exit 1
    fi
    mkdir -p "$(dirname "$target")"
    cp -- "$source" "$target"
}

copy_file "$binary" "$staging/cdx"
copy_file "$root/LICENSE" "$staging/LICENSE"
copy_file "$root/README.md" "$staging/README.md"
copy_file "$root/docs/packaging.md" "$staging/docs/packaging.md"
copy_file "$root/docs/install-update.md" "$staging/docs/install-update.md"
copy_file "$root/examples/cordonix-install-update-prompt.md" "$staging/examples/cordonix-install-update-prompt.md"
copy_file "$root/examples/codex-config.toml" "$staging/examples/codex-config.toml"
copy_file "$root/examples/read-lines.json" "$staging/examples/read-lines.json"
copy_file "$root/examples/read-first.json" "$staging/examples/read-first.json"
copy_file "$root/examples/read-tail.json" "$staging/examples/read-tail.json"
copy_file "$root/examples/read-raw.json" "$staging/examples/read-raw.json"
copy_file "$root/examples/list-directory.json" "$staging/examples/list-directory.json"
copy_file "$root/examples/resolve-path.json" "$staging/examples/resolve-path.json"
copy_file "$root/examples/verify-encoding.json" "$staging/examples/verify-encoding.json"

source_epoch=${SOURCE_DATE_EPOCH:-0}
case "$source_epoch" in
    ''|*[!0-9]*) echo "SOURCE_DATE_EPOCH must be an unsigned integer" >&2; exit 2 ;;
esac
revision=$(git rev-parse HEAD)
dirty_paths=$(git status --porcelain=v1 --untracked-files=normal)

files='cdx LICENSE README.md docs/packaging.md docs/install-update.md examples/cordonix-install-update-prompt.md examples/codex-config.toml examples/read-lines.json examples/read-first.json examples/read-tail.json examples/read-raw.json examples/list-directory.json examples/resolve-path.json examples/verify-encoding.json'
manifest="$staging/manifest.json"
{
    printf '{"format_version":1,"name":"Cordonix","version":"%s","target":"linux-x64","source_date_epoch":%s,"files":[' "$version" "$source_epoch"
    first=1
    for name in $files; do
        path="$staging/$name"
        size=$(wc -c < "$path" | tr -d ' ')
        sha256=$(sha256sum "$path" | cut -d ' ' -f 1)
        if [ "$first" -eq 0 ]; then printf ','; fi
        printf '{"path":"%s","size_bytes":%s,"sha256":"%s"}' "$name" "$size" "$sha256"
        first=0
    done
    printf ']}'
    printf '\n'
} > "$manifest"

provenance="$staging/provenance.json"
dirty_json='[]'
printf '{"format_version":1,"name":"Cordonix","version":"%s","target":"linux-x64","checkout_revision_at_packaging":"%s","checkout_dirty_at_packaging":%s,"checkout_status_at_packaging":%s,"binary_build_revision":"UNKNOWN","verified_executable_version":"Cordonix cdx %s","input_executable_sha256":"%s","source_date_epoch":%s,"builder":"eng/package-linux.sh","signature":"NOT_RUN"}\n' \
    "$version" "$revision" "$([ -n "$dirty_paths" ] && printf true || printf false)" "$dirty_json" "$version" "$(sha256sum "$binary" | cut -d ' ' -f 1)" "$source_epoch" > "$provenance"

sbom="$staging/sbom.spdx.json"
{
    printf '{"spdxVersion":"SPDX-2.3","dataLicense":"CC0-1.0","SPDXID":"SPDXRef-DOCUMENT","name":"Cordonix-%s-linux-x64","documentNamespace":"https://github.com/IoTSharp/Cordonix/spdx/%s","creationInfo":{"created":"1970-01-01T00:00:00Z","creators":["Tool: Cordonix eng/package-linux.sh"]},"packages":[{"SPDXID":"SPDXRef-Package-Cordonix","name":"Cordonix","versionInfo":"%s","downloadLocation":"NOASSERTION","filesAnalyzed":true,"packageVerificationCode":{"packageVerificationCodeValue":"NOT_COMPUTED","packageVerificationCodeExcludedFiles":["./sbom.spdx.json"]},"licenseConcluded":"NOASSERTION","licenseDeclared":"Apache-2.0","licenseInfoFromFiles":["NOASSERTION"],"copyrightText":"NOASSERTION"}],"files":[' "$version" "linux-$version" "$version"
    first=1
    for name in $files manifest.json provenance.json; do
        path="$staging/$name"
        sha1=$(sha1sum "$path" | cut -d ' ' -f 1)
        sha256=$(sha256sum "$path" | cut -d ' ' -f 1)
        spdx_id=$(printf '%s' "$name" | tr '/ ' '--')
        if [ "$first" -eq 0 ]; then printf ','; fi
        printf '{"fileName":"./%s","SPDXID":"SPDXRef-File-%s","checksums":[{"algorithm":"SHA1","checksumValue":"%s"},{"algorithm":"SHA256","checksumValue":"%s"}],"licenseConcluded":"NOASSERTION","licenseInfoInFiles":["NOASSERTION"],"copyrightText":"NOASSERTION"}' "$name" "$spdx_id" "$sha1" "$sha256"
        first=0
    done
    printf '],"relationships":[{"spdxElementId":"SPDXRef-DOCUMENT","relationshipType":"DESCRIBES","relatedSpdxElement":"SPDXRef-Package-Cordonix"}]}'
    printf '\n'
} > "$sbom"

for metadata in manifest.json provenance.json sbom.spdx.json; do
    cp -- "$staging/$metadata" "$output_dir/cordonix-$version-linux-x64.$metadata"
done

archive="$output_dir/cordonix-$version-linux-x64.tar.gz"
tar --sort=name --mtime="@$source_epoch" --owner=0 --group=0 --numeric-owner -cf "$task_root/archive.tar" -C "$staging" .
gzip -n -c "$task_root/archive.tar" > "$archive"
archive_sha256=$(sha256sum "$archive" | cut -d ' ' -f 1)
printf '%s  %s\n' "$archive_sha256" "$(basename "$archive")" > "$archive.sha256"

if ! tar -tzf "$archive" > "$task_root/listing.txt"; then
    echo "Created Linux archive could not be read back" >&2
    exit 1
fi
success=1
printf '{"status":"PASS","archive":"%s","archive_sha256":"%s","version":"%s","target":"linux-x64","signature":"NOT_RUN"}\n' "$archive" "$archive_sha256" "$version"
