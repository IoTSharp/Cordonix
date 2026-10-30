#!/bin/sh
set -eu
export LC_ALL=C

if [ "${CDX_PACKAGE_BOUNDED:-0}" != 1 ]; then
    CDX_PACKAGE_BOUNDED=1
    export CDX_PACKAGE_BOUNDED
    exec timeout --kill-after=5s 120s sh "$0" "$@"
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
version=${1:-${CDX_VERSION:-0.1.0-dev}}
case "$version" in
    ''|*[!0-9A-Za-z.+_-]*) echo "Invalid release version: $version" >&2; exit 2 ;;
esac
if [ "${#version}" -gt 64 ]; then echo 'Release version exceeds 64 characters.' >&2; exit 2; fi

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
case "$output_dir" in /*) ;; *) echo 'OUTPUT_DIRECTORY must be absolute.' >&2; exit 2 ;; esac
mkdir -p "$output_dir"
output_dir=$(CDPATH= cd -- "$output_dir" && pwd -P)
case "$output_dir" in "$root/build/"*) ;; *) echo 'Package output must remain below build/.' >&2; exit 2 ;; esac
task_root=$(mktemp -d "$output_dir/.package-linux.XXXXXX")
case "$task_root" in "$output_dir/"*) ;; *) echo 'Temporary package directory escaped its owner.' >&2; exit 2 ;; esac
staging="$task_root/staging"
success=0
cleanup() {
    status=$?
    trap - EXIT HUP INT TERM
    if [ "$success" -eq 1 ]; then rm -rf -- "$task_root"
    else echo "Package failed; task-owned diagnostics retained at $task_root" >&2; fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM
mkdir -p "$staging"

# Git quotes control characters in porcelain paths; preserve those lines as JSON strings.
json_lines() {
    awk 'BEGIN { printf "[" } NR > 100 { exit 2 } {
        if (NR > 1) printf ","; printf "\"";
        for (i = 1; i <= length($0); ++i) {
            c = substr($0, i, 1); if (c == "\\" || c == "\"") printf "\\"; printf "%s", c;
        }
        printf "\"";
    } END { printf "]" }' "$1"
}

files='cdx LICENSE README.md docs/packaging.md docs/execution-boundaries.md docs/install-update.md install/install.ps1 install/install.sh examples/cordonix-install-update-prompt.md examples/codex-config.toml examples/request.json examples/read-lines.json examples/read-first.json examples/read-tail.json examples/read-raw.json examples/list-directory.json examples/resolve-path.json examples/verify-encoding.json'
printf '%s\n' $files | sort > "$task_root/payload-list.txt"
for name in $files; do
    source="$root/$name"
    if [ "$name" = cdx ]; then source=$binary; fi
    if [ ! -f "$source" ] || [ "$(wc -c < "$source")" -gt 33554432 ]; then
        echo "Package input is missing or exceeds 32 MiB: $source" >&2
        exit 1
    fi
    mkdir -p "$(dirname "$staging/$name")"
    cp -- "$source" "$staging/$name"
    case "$name" in cdx) chmod 755 "$staging/$name" ;; *) chmod 644 "$staging/$name" ;; esac
done

source_epoch=${SOURCE_DATE_EPOCH:-0}
case "$source_epoch" in ''|*[!0-9]*) echo 'SOURCE_DATE_EPOCH must be an unsigned integer.' >&2; exit 2 ;; esac
epoch_text=$(date -u -d "@$source_epoch" '+%Y-%m-%dT%H:%M:%SZ')
revision=$(timeout --kill-after=2s 15s git rev-parse HEAD)
timeout --kill-after=2s 15s git -c core.quotePath=true status --porcelain=v1 --untracked-files=normal > "$task_root/git-status.txt"
if [ "$(wc -l < "$task_root/git-status.txt")" -gt 100 ]; then echo 'Checkout has more than 100 changed paths.' >&2; exit 1; fi
dirty_json=$(json_lines "$task_root/git-status.txt")
dirty=false
if [ -s "$task_root/git-status.txt" ]; then dirty=true; fi

manifest="$staging/manifest.json"
{
    printf '{"format_version":1,"name":"Cordonix","version":"%s","target":"linux-x64","scope":"payload files; generated metadata is described by SPDX and the archive hash","source_date_epoch":%s,"files":[' "$version" "$source_epoch"
    first=1
    for name in $(cat "$task_root/payload-list.txt"); do
        path="$staging/$name"
        if [ "$first" -eq 0 ]; then printf ','; fi
        printf '{"path":"%s","size_bytes":%s,"sha256":"%s"}' "$name" "$(wc -c < "$path" | tr -d ' ')" "$(sha256sum "$path" | cut -d ' ' -f 1)"
        first=0
    done
    printf ']}\n'
} > "$manifest"

timeout --kill-after=2s 15s "$binary" --request "$root/examples/request.json" > "$task_root/build-info.json"
timeout --kill-after=2s 15s python3 - "$task_root/build-info.json" "$staging/provenance.json" "$version" "$revision" "$dirty" "$dirty_json" "$binary" "$source_epoch" <<'PY'
import hashlib, json, os, pathlib, re, sys
info = json.loads(pathlib.Path(sys.argv[1]).read_text())
if info.get('status') != 'ok' or info.get('version') != sys.argv[3] or not re.fullmatch(r'(UNKNOWN|[0-9a-f]{40})', info.get('build_revision', '')):
    raise SystemExit('Invalid executable build metadata')
if os.environ.get('CDX_REQUIRE_CLEAN_BUILD') == '1' and (sys.argv[5] != 'false' or info['build_dirty'] is not False or info['build_revision'] != sys.argv[4]):
    raise SystemExit('Release requires a clean build of the packaging revision')
data = dict(format_version=1, name='Cordonix', version=sys.argv[3], target='linux-x64',
    checkout_revision_at_packaging=sys.argv[4], checkout_dirty_at_packaging=sys.argv[5] == 'true',
    checkout_status_at_packaging=json.loads(sys.argv[6]), binary_build_revision=info['build_revision'],
    binary_build_dirty=info['build_dirty'],
    provenance_boundary='Build-script revision and dirty flag embedded in the executable and independently read back. This is not a signed builder attestation.',
    verified_executable_version='Cordonix cdx ' + sys.argv[3], input_executable_sha256=hashlib.sha256(pathlib.Path(sys.argv[7]).read_bytes()).hexdigest(),
    source_date_epoch=int(sys.argv[8]), builder='eng/package-linux.sh', signature='NOT_RUN')
pathlib.Path(sys.argv[2]).write_text(json.dumps(data, separators=(',', ':')) + '\n')
PY

printf '%s\n' $files manifest.json provenance.json | sort > "$task_root/analyzed-list.txt"
{
    printf '['
    first=1
    for name in $(cat "$task_root/analyzed-list.txt"); do
        path="$staging/$name"
        if [ "$first" -eq 0 ]; then printf ','; fi
        spdx_id=$(printf '%s' "$name" | tr -c 'A-Za-z0-9.-' '-')
        printf '{"fileName":"./%s","SPDXID":"SPDXRef-File-%s","checksums":[{"algorithm":"SHA1","checksumValue":"%s"},{"algorithm":"SHA256","checksumValue":"%s"}],"licenseConcluded":"NOASSERTION","licenseInfoInFiles":["NOASSERTION"],"copyrightText":"NOASSERTION"}' \
            "$name" "$spdx_id" "$(sha1sum "$path" | cut -d ' ' -f 1)" "$(sha256sum "$path" | cut -d ' ' -f 1)"
        first=0
    done
    printf ']'
} > "$task_root/spdx-files.json"
for name in $(cat "$task_root/analyzed-list.txt"); do sha1sum "$staging/$name" | cut -d ' ' -f 1; done > "$task_root/sha1.txt"
verification_code=$(sort "$task_root/sha1.txt" | tr -d '\n' | sha1sum | cut -d ' ' -f 1)
content_hash=$(sha256sum "$task_root/spdx-files.json" | cut -d ' ' -f 1)
{
    printf '{"spdxVersion":"SPDX-2.3","dataLicense":"CC0-1.0","SPDXID":"SPDXRef-DOCUMENT","name":"Cordonix-%s-linux-x64","documentNamespace":"https://github.com/IoTSharp/Cordonix/spdx/%s","creationInfo":{"created":"%s","creators":["Tool: Cordonix eng/package-linux.sh"]},"packages":[{"SPDXID":"SPDXRef-Package-Cordonix","name":"Cordonix","versionInfo":"%s","downloadLocation":"NOASSERTION","filesAnalyzed":true,"packageVerificationCode":{"packageVerificationCodeValue":"%s","packageVerificationCodeExcludedFiles":["./sbom.spdx.json"]},"licenseConcluded":"NOASSERTION","licenseDeclared":"Apache-2.0","licenseInfoFromFiles":["NOASSERTION"],"copyrightText":"NOASSERTION"}],"files":' "$version" "$content_hash" "$epoch_text" "$version" "$verification_code"
    cat "$task_root/spdx-files.json"
    printf ',"relationships":[{"spdxElementId":"SPDXRef-DOCUMENT","relationshipType":"DESCRIBES","relatedSpdxElement":"SPDXRef-Package-Cordonix"}'
    for name in $(cat "$task_root/analyzed-list.txt"); do
        spdx_id=$(printf '%s' "$name" | tr -c 'A-Za-z0-9.-' '-')
        printf ',{"spdxElementId":"SPDXRef-Package-Cordonix","relationshipType":"CONTAINS","relatedSpdxElement":"SPDXRef-File-%s"}' "$spdx_id"
    done
    printf ']}\n'
} > "$staging/sbom.spdx.json"

archive_name="cordonix-$version-linux-x64.tar.gz"
archive="$output_dir/$archive_name"
timeout --kill-after=2s 30s tar --sort=name --mtime="@$source_epoch" --owner=0 --group=0 --numeric-owner -cf "$task_root/archive.tar" -C "$staging" .
timeout --kill-after=2s 30s gzip -n -c "$task_root/archive.tar" > "$task_root/$archive_name"
archive_sha256=$(sha256sum "$task_root/$archive_name" | cut -d ' ' -f 1)
timeout --kill-after=2s 15s tar -tzf "$task_root/$archive_name" > "$task_root/listing.txt"
mv -- "$task_root/$archive_name" "$archive"
for metadata in manifest.json provenance.json sbom.spdx.json; do
    cp -- "$staging/$metadata" "$output_dir/cordonix-$version-linux-x64.$metadata"
done
printf '%s  %s\n' "$archive_sha256" "$archive_name" > "$archive.sha256"
success=1
printf '{"status":"PASS","archive":"%s","archive_sha256":"%s","version":"%s","target":"linux-x64","spdx_verification":"PASS","signature":"NOT_RUN"}\n' "$archive" "$archive_sha256" "$version"
