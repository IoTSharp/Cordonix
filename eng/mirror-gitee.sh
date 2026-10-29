#!/bin/sh
set -eu

tag=${1:-}
if ! printf '%s\n' "$tag" | grep -Eq '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'; then
    echo 'Expected a stable tag such as v0.0.1.' >&2
    exit 2
fi
if [ -z "${GITEE_TOKEN:-}" ]; then
    echo 'GITEE_TOKEN is required to mirror the release.' >&2
    exit 2
fi

task_root=$(mktemp -d)
source_ref="refs/cordonix-release/mirror-$$"
cleanup() {
    result=$?
    trap - EXIT HUP INT TERM
    git update-ref -d "$source_ref" || true
    rm -f -- "$task_root/askpass.sh"
    rmdir -- "$task_root"
    exit "$result"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM

printf '%s\n' '#!/bin/sh' 'case "$1" in *Username*) printf "%s\n" oauth2;; *) printf "%s\n" "$GITEE_TOKEN";; esac' > "$task_root/askpass.sh"
chmod 700 "$task_root/askpass.sh"
export GIT_ASKPASS="$task_root/askpass.sh"
export GIT_TERMINAL_PROMPT=0
target=https://gitee.com/IoTSharp/Cordonix.git

# checkout can replace an annotated event tag with its peeled commit locally.
timeout --kill-after=5s 90s git fetch --no-tags origin "refs/tags/$tag:$source_ref"
source_object=$(git rev-parse "$source_ref")
source_commit=$(git rev-parse "$source_ref^{commit}")
target_object=$(timeout --kill-after=5s 30s git ls-remote --refs "$target" "refs/tags/$tag" | cut -f 1)
if [ -n "$target_object" ] && [ "$target_object" != "$source_object" ]; then
    echo "Gitee already has a different object for $tag; published tags are immutable." >&2
    exit 1
fi
timeout --kill-after=5s 90s git push "$target" refs/remotes/origin/main:refs/heads/main
if [ -z "$target_object" ]; then
    timeout --kill-after=5s 90s git push "$target" "$source_ref:refs/tags/$tag"
fi
readback=$(timeout --kill-after=5s 30s git ls-remote --refs "$target" "refs/tags/$tag" | cut -f 1)
if [ "$readback" != "$source_object" ]; then
    echo 'Gitee tag readback differs from the GitHub tag object.' >&2
    exit 1
fi
printf 'Mirrored %s: object=%s commit=%s\n' "$tag" "$source_object" "$source_commit"
