#!/usr/bin/env bash
# shnolk's Monomodule (AGPL-3.0) at the pinned commit, and the dsp56300 revision Monomodule pins, with
# Monomodule's patch (ext/patches/0001-dsp56300-mnm.patch) applied, as Monomodule's own build prepares it.
#   tools/fetch-core.sh [CORE_DIR] [DSP56300_DIR]
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
core=${1:-"$root/_deps/monomodule"}
dsp=${2:-"$root/_deps/dsp56300"}
commit=23e541021b92d1a5978f36c0a552ac443376df1a
dspCommit=c051afad31612c2d2c7a81a7ab23e1c5ac9e61af
pin() {   # pin DIR URL COMMIT
    local dir=$1 url=$2 want=$3 fresh=false
    if [ ! -d "$dir/.git" ] && [ ! -f "$dir/.git" ]; then
        fresh=true
        git clone --no-checkout --filter=blob:none "$url" "$dir"
    fi
    if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null || true)" != "$want" ]; then
        if [ "$fresh" = false ] && [ -n "$(git -C "$dir" status --porcelain)" ]; then
            echo "$dir has changes; refusing to replace them" >&2; exit 1
        fi
        git -C "$dir" fetch --depth 1 origin "$want"
        git -C "$dir" checkout --detach "$want"
    fi
}
pin "$core" https://github.com/shnolk/monomodule.git "$commit"
pin "$dsp" https://github.com/dsp56300/dsp56300.git "$dspCommit"
git -C "$dsp" submodule update --init --depth 1 source/asmjit
# Applied once: a checkout that already carries the patch is left as it is.
patch="$core/ext/patches/0001-dsp56300-mnm.patch"
if git -C "$dsp" apply --check --whitespace=nowarn "$patch" 2>/dev/null; then
    git -C "$dsp" apply --whitespace=nowarn "$patch"
elif ! git -C "$dsp" apply --check --reverse --whitespace=nowarn "$patch" 2>/dev/null; then
    echo "$dsp neither takes nor already carries $patch" >&2; exit 1
fi
