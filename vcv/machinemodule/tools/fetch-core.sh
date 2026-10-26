#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
dest=${1:-"$root/_deps/machinemodule"}
commit=a2b6cf2478cbe94dfe30bc42da39e221df0e2556
fresh=false
if [ ! -d "$dest/.git" ] && [ ! -f "$dest/.git" ]; then
    fresh=true
    git clone --no-checkout --filter=blob:none https://github.com/joelanders/mpc-vst-machinedrum.git "$dest"
fi
if [ "$(git -C "$dest" rev-parse HEAD 2>/dev/null || true)" != "$commit" ]; then
    if [ "$fresh" = false ] && [ -n "$(git -C "$dest" status --porcelain)" ]; then
        echo "Core checkout has changes; refusing to replace them" >&2; exit 1
    fi
    git -C "$dest" fetch --depth 1 origin "$commit"
    git -C "$dest" checkout --detach "$commit"
fi
git -C "$dest" submodule update --init --depth 1 libs/dsp56300 libs/gearmulator-md-mm
git -C "$dest/libs/dsp56300" submodule update --init --depth 1 source/asmjit
git -C "$dest/libs/gearmulator-md-mm" submodule update --init --depth 1 source/mc68k
