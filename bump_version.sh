#!/usr/bin/env bash
set -euo pipefail

CMAKELISTS="CMakeLists.txt"

usage() {
    echo "Usage: $0 {major|minor|patch}"
    exit 1
}

if [[ $# -ne 1 ]]; then
    usage
fi

case "$1" in
    major|minor|patch) ;;
    *) usage ;;
esac

current=$(sed -n 's/^[[:space:]]*set(FIRMWARE_VERSION "v\([0-9.]*\)")/\1/p' "$CMAKELISTS")
if [[ -z "$current" ]]; then
    echo "Error: could not find FIRMWARE_VERSION in $CMAKELISTS"
    exit 1
fi

IFS='.' read -ra parts <<< "$current"
major="${parts[0]:-0}"
minor="${parts[1]:-0}"
patch="${parts[2]:-0}"

case "$1" in
    major)
        major=$((major + 1))
        minor=0
        patch=0
        ;;
    minor)
        minor=$((minor + 1))
        patch=0
        ;;
    patch)
        patch=$((patch + 1))
        ;;
esac

new="v${major}.${minor}.${patch}"

if [[ "$(uname -s)" == "Darwin" ]]; then
    sed -i '' "s/set(FIRMWARE_VERSION \"v${current}\")/set(FIRMWARE_VERSION \"${new}\")/" "$CMAKELISTS"
else
    sed -i "s/set(FIRMWARE_VERSION \"v${current}\")/set(FIRMWARE_VERSION \"${new}\")/" "$CMAKELISTS"
fi

echo "$current -> $new"
