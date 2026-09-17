#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Regenerates docs/screenshots/*.png from every examples/*/Main.qml with
# qtk-preview (headless: offscreen platform + software renderer).
#
#   tools/scripts/screenshots.sh              # all examples, 1280x800
#   tools/scripts/screenshots.sh gallery      # one example
#   QTK_PREVIEW=/path/to/qtk-preview tools/scripts/screenshots.sh
#
# The gallery is also rendered at comfortable density and in the light
# preset, because those are the variants a control must survive. Exits
# non-zero when any render fails, so CI and agents notice.
set -u

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="$root/docs/screenshots"
size="${QTK_SIZE:-1280x800}"
mkdir -p "$out"

preview="${QTK_PREVIEW:-}"
if [ -z "$preview" ]; then
    for candidate in "$root/build/dev/tools/preview/qtk-preview" "$root"/build/*/tools/preview/qtk-preview; do
        if [ -x "$candidate" ]; then
            preview="$candidate"
            break
        fi
    done
fi
if [ -z "$preview" ] || [ ! -x "$preview" ]; then
    echo "screenshots.sh: no qtk-preview binary found; build the dev preset first" >&2
    exit 2
fi

export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
export QT_QUICK_BACKEND="${QT_QUICK_BACKEND:-software}"

status=0
render() {
    local name="$1" file="$2" png="$3"
    shift 3
    if "$preview" "$file" --grab "$png" --size "$size" --wait 250 "$@" >/dev/null; then
        echo "ok   $png"
    else
        echo "FAIL $name ($file)" >&2
        status=1
    fi
}

selected="${1:-}"
found=0
for dir in "$root"/examples/*/; do
    name="$(basename "$dir")"
    file="$dir/Main.qml"
    [ -f "$file" ] || continue
    if [ -n "$selected" ] && [ "$selected" != "$name" ]; then
        continue
    fi
    found=1
    render "$name" "$file" "$out/$name.png"
    if [ "$name" = "gallery" ]; then
        render "$name (comfortable)" "$file" "$out/gallery-comfortable.png" --density comfortable
        render "$name (light)" "$file" "$out/gallery-light.png" --theme sloom-light
    fi
done

if [ "$found" -eq 0 ]; then
    echo "screenshots.sh: no examples/*/Main.qml matched" >&2
    exit 2
fi
exit $status
