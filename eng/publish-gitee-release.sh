#!/bin/sh
set -eu

if [ "$#" -lt 5 ] || [ "$#" -gt 6 ]; then
    echo "usage: publish-gitee-release.sh OWNER REPOSITORY TAG TARGET_COMMIT ASSET_ROOT [BODY_FILE]" >&2
    exit 2
fi
owner=$1
repository=$2
tag=$3
target_commit=$4
asset_root=$5
body_file=${6:-}

case "$owner/$repository" in
    ''|*[!A-Za-z0-9._/-]*) echo "Invalid Gitee repository identifier" >&2; exit 2 ;;
esac
if ! printf '%s\n' "$tag" | grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "Release tag must use v<major>.<minor>.<patch>: $tag" >&2
    exit 2
fi
case "$target_commit" in
    [0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]*) ;;
    *) echo "Target commit must be a hexadecimal revision" >&2; exit 2 ;;
esac
if [ -z "${GITEE_TOKEN:-}" ]; then
    echo 'GITEE_TOKEN is required for Gitee release publication.' >&2
    exit 2
fi
case "$asset_root" in
    /*) ;;
    *) echo "Asset root must be an absolute path" >&2; exit 2 ;;
esac
if [ ! -d "$asset_root" ]; then
    echo "Asset root does not exist: $asset_root" >&2
    exit 1
fi
if [ -n "$body_file" ] && [ ! -f "$body_file" ]; then
    echo "Release body file does not exist: $body_file" >&2
    exit 1
fi

task_root=$(mktemp -d)
cleanup() {
    status=$?
    rm -rf -- "$task_root"
    trap - EXIT HUP INT TERM
    exit "$status"
}
trap cleanup EXIT HUP INT TERM

asset_list="$task_root/assets.txt"
find "$asset_root" -maxdepth 2 -type f -print | sort > "$asset_list"
asset_count=$(wc -l < "$asset_list" | tr -d ' ')
if [ "$asset_count" -lt 1 ] || [ "$asset_count" -gt 32 ]; then
    echo "Expected 1..32 release assets, found $asset_count" >&2
    exit 1
fi
asset_names="$task_root/asset-names.txt"
: > "$asset_names"
while IFS= read -r asset; do
    name=$(basename "$asset")
    case "$name" in
        ''|*+*|*'"'*|*' '*) echo "Invalid or ambiguous Gitee asset name: $name" >&2; exit 2 ;;
    esac
    if grep -Fqx -- "$name" "$asset_names"; then
        echo "Duplicate release asset name: $name" >&2
        exit 2
    fi
    printf '%s\n' "$name" >> "$asset_names"
done < "$asset_list"

api_root='https://gitee.com/api/v5'
repository_api="$api_root/repos/$owner/$repository"

request() {
    method=$1
    uri=$2
    output=$3
    shift 3
    curl --silent --show-error --fail-with-body --max-time 30 --retry 0 \
        -X "$method" -H "Accept: application/json" -H "Authorization: token $GITEE_TOKEN" "$uri" "$@" > "$output"
}

repository_json="$task_root/repository.json"
if ! request GET "$repository_api" "$repository_json"; then
    echo "Gitee repository is not reachable or does not exist: $owner/$repository" >&2
    exit 1
fi

tag_json="$task_root/tag.json"
tag_status=$(curl --silent --show-error --max-time 30 --retry 0 \
    -o "$tag_json" -w '%{http_code}' -H "Accept: application/json" -H "Authorization: token $GITEE_TOKEN" \
    "$repository_api/releases/tags/$tag")
release_id=''
if [ "$tag_status" = '200' ]; then
    release_id=$(jq -r '.id // empty' "$tag_json")
elif [ "$tag_status" != '404' ]; then
    echo "Gitee release lookup failed with HTTP $tag_status" >&2
    exit 1
fi

if [ -z "$release_id" ]; then
    release_body="Cordonix $tag\n\nWindows and Linux assets are available here and on GitHub: https://github.com/IoTSharp/Cordonix/releases/tag/$tag"
    if [ -n "$body_file" ]; then
        release_body=$(cat -- "$body_file")
    fi
    created_json="$task_root/created.json"
    request POST "$repository_api/releases" "$created_json" \
        --data-urlencode "tag_name=$tag" \
        --data-urlencode "name=$tag" \
        --data-urlencode "target_commitish=$target_commit" \
        --data-urlencode "body=$release_body"
    release_id=$(jq -r '.id // empty' "$created_json")
    if [ -z "$release_id" ]; then
        echo 'Gitee did not return a release id.' >&2
        exit 1
    fi
fi

release_json="$task_root/release.json"
request GET "$repository_api/releases/$release_id" "$release_json"
while IFS= read -r asset; do
    name=$(basename "$asset")
    if jq -e --arg name "$name" '[.assets[]?.name] | index($name) != null' "$release_json" >/dev/null; then
        continue
    fi
    request POST "$repository_api/releases/$release_id/attach_files" "$task_root/upload.json" -F "file=@$asset"
    request GET "$repository_api/releases/$release_id" "$release_json"
done < "$asset_list"

while IFS= read -r name; do
    if ! jq -e --arg name "$name" '[.assets[]?.name] | index($name) != null' "$release_json" >/dev/null; then
        echo "Gitee release is missing asset after upload: $name" >&2
        exit 1
    fi
done < "$asset_names"

printf '{"status":"PASS","owner":"%s","repository":"%s","tag":"%s","release_id":%s,"assets":%s}\n' \
    "$owner" "$repository" "$tag" "$release_id" "$(jq -c '[.assets[]?.name]' "$release_json")"
