#!/bin/sh
# Headless render of programs 1-5 to WAV. Needs `pd` on PATH (0.52+).
# Usage: tools/render.sh [outdir]
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mkdir -p "${1:-$HERE/renders}" && cd "${1:-$HERE/renders}" && pwd)
TMP=$(mktemp -d)
python3 "$HERE/tools/mkseq.py" "$OUT" "$TMP/seq.txt"
cat > "$TMP/run.pd" <<PD
#N canvas 0 0 400 300 12;
#X obj 20 20 loadbang;
#X msg 20 50 \; pd dsp 1;
#X obj 120 20 vastish;
#X obj 20 80 delay 200;
#X msg 20 110 read $TMP/seq.txt \, bang;
#X obj 20 140 qlist;
#X obj 200 80 r dummy;
#X connect 0 0 1 0;
#X connect 0 0 3 0;
#X connect 3 0 4 0;
#X connect 4 0 5 0;
PD
pd -nosound -nomidi -batch -stderr -path "$HERE" -open "$TMP/run.pd"
rm -rf "$TMP"
python3 "$HERE/tools/analyze.py" "$OUT"
