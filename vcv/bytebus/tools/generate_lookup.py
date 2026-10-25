#!/usr/bin/env python3
"""Generate Lookup v1 from the formulas below; no imported waveform/pattern data."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
N, SHAPES = 256, 64
FAMILIES = ["Harmonic", "Formant", "Fold", "Curve", "Walsh", "Rhythm"]

def fnv(data):
    h = 14695981039346656037
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xffffffffffffffff
    return h

def mix(v):
    v &= 0xffffffff
    v ^= v >> 16
    v = (v * 0x7feb352d) & 0xffffffff
    v ^= v >> 15
    v = (v * 0x846ca68b) & 0xffffffff
    return v ^ (v >> 16)

def quantize_audio(values):
    mean = sum(values) / N
    values = [v - mean for v in values]
    rms = math.sqrt(sum(v*v for v in values) / N)
    gain = min(.5 / max(rms, 1e-12), .94 / max(abs(v) for v in values))
    return bytes(max(0, min(255, math.floor(128 + 128*v*gain + .5))) for v in values)

def walsh_masks(shape):
    # Reversible row additions/swaps preserve rank over GF(2).
    masks = [1 << k for k in range(8)]
    state = mix(shape + 0x42555331)
    for _ in range(shape * 2):
        state = mix(state + 1)
        a, b = state & 7, (state >> 3) & 7
        if a != b:
            masks[a] ^= masks[b]
            if state & 64:
                masks[a], masks[b] = masks[b], masks[a]
    return masks

def rhythm(shape, step):
    # Authored structural anchors plus arithmetic pulse distribution and ghosts.
    density, rotation = shape & 7, shape >> 3
    word = int(step in (0, 16) or (density >= 4 and step == 24))
    word |= int(step in (8, 24)) << 1
    counts = [8 + density * 2, 2 + density // 2, 3 + density, 1 + density // 2,
              2 + density // 2, 1 + density // 3]
    for lane, count in enumerate(counts, 2):
        offset = (rotation * (2 * lane + 1) + lane * 3) & 31
        hit = ((step + offset) * count) % 32 < count
        # Open-hat lane avoids the fixed backbeat; ghosts have a sparse voice.
        if lane == 3 and step in (8, 24):
            hit = False
        word |= int(hit) << lane
    return word

def build():
    data = bytearray()
    records = []
    for family in FAMILIES:
        tables = []
        for shape in range(SHAPES):
            theta = 2 * math.pi * shape / SHAPES
            if family == "Harmonic":
                values = []
                for n in range(N):
                    p = 2 * math.pi * n / N
                    values.append(math.sin(p) + .6*(.5+.5*math.cos(theta))*math.sin(2*p)
                                  + .42*math.sin(theta)*math.sin(3*p + .3*math.sin(theta))
                                  + .22*(.5+.5*math.sin(theta))*math.sin(5*p))
                table = quantize_audio(values)
            elif family == "Formant":
                centers = [2.5+1.2*math.cos(theta), 8+3*math.sin(theta), 18+5*math.cos(theta+.6)]
                widths, weights = [.8, 1.7, 3.], [1., .5, .25]
                amps = [.12 if h == 1 else 0. for h in range(1, 33)]
                for h in range(1, 33):
                    amps[h-1] += sum(w*math.exp(-.5*((h-c)/b)**2) for c,b,w in zip(centers,widths,weights)) / h**.35
                values = [sum(a*math.cos(2*math.pi*h*n/N + math.pi*h*(h-1)/32)
                              for h,a in enumerate(amps, 1)) for n in range(N)]
                table = quantize_audio(values)
            elif family == "Fold":
                drive, bias = 1 + 3*(.5+.5*math.cos(theta)), .35*math.sin(theta)
                table = quantize_audio([2/math.pi*math.asin(math.sin(math.pi*(drive*math.sin(2*math.pi*n/N)+bias)/2)) for n in range(N)])
            elif family == "Curve":
                anchor, frac = divmod(shape, 8)
                blend = (1-math.cos(math.pi*frac/8))/2
                values = []
                for n in range(N):
                    u = n/255
                    s = u*u*(3-2*u)
                    anchors = [u, u**3, s, math.sqrt(u), 1-u, 1-u**3, 1-s, 1-math.sqrt(u)]
                    v = (1-blend)*anchors[anchor] + blend*anchors[(anchor+1)%8]
                    values.append(max(0,min(255,math.floor(v*255+.5))))
                table = bytes(values)
            elif family == "Walsh":
                masks = walsh_masks(shape)
                table = bytes(sum(((n & mask).bit_count() & 1) << lane for lane,mask in enumerate(masks)) for n in range(N))
                assert len(set(table)) == N
            else:
                table = bytes(rhythm(shape,n//8) if n%8 < 4 else 0 for n in range(N))
            tables.append(table)
        for shape, table in enumerate(tables):
            extra = {}
            if family in FAMILIES[:3]:
                values = [(v-128)/128 for v in table]
                rms = math.sqrt(sum(v*v for v in values)/N)
                dc = sum(values)/N
                assert .25 < rms < .52 and abs(dc) < 1/128, (family, shape, rms, dc)
                neighbor = tables[(shape+1)%SHAPES]
                delta = math.sqrt(sum(((a-b)/128)**2 for a,b in zip(table,neighbor))/N)
                extra = {"rms":round(rms,8),"dc":round(dc,8),"next_shape_rms_delta":round(delta,8)}
            if family == "Walsh":
                extra["masks"] = walsh_masks(shape)
            records.append({"family":family,"shape":shape,"sha256":hashlib.sha256(table).hexdigest(),**extra})
            data.extend(table)
    header = b"BBLOOK01" + bytes([len(FAMILIES), SHAPES, 0, 1])
    packed = header + data
    manifest = {
        "format":"ByteBusLab.LookupLibrary", "version":1, "generator":"generate_lookup.py v1",
        "seed":0,"families":FAMILIES,"shapes_per_family":SHAPES,"values_per_shape":N,
        "sha256":hashlib.sha256(packed).hexdigest(),"data_fnv1a64":f"{fnv(data):016x}",
        "provenance":"Original analytic formulas and arithmetic pattern rules in generate_lookup.py; no external table data.",
        "tables":records,
    }
    identity = ("// Generated by tools/generate_lookup.py. Do not edit.\n#pragma once\n#include <cstdint>\n"
                "namespace dspemu::lookup {\n"
                f"constexpr uint64_t LIBRARY_HASH = 0x{fnv(data):016x}ull;\n"
                f'constexpr const char* LIBRARY_SHA256 = "{manifest["sha256"]}";\n'
                "} // namespace dspemu::lookup\n")
    return packed, json.dumps(manifest,indent=2,sort_keys=True)+"\n", identity

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check",action="store_true",help="Regenerate and compare committed assets")
    args = parser.parse_args()
    packed, manifest, identity = build()
    files = {ROOT/"res/lookup-v1.bin":packed,ROOT/"res/lookup-v1.json":manifest.encode(),ROOT/"src/engine/LibraryIdentity.hpp":identity.encode()}
    for path, content in files.items():
        if args.check:
            if not path.exists() or path.read_bytes() != content:
                raise SystemExit(f"Generated asset differs: {path}")
        else:
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(content)
    print(f"PASS: original Lookup v1, 6 families x 64 shapes x 256 bytes, SHA256 {hashlib.sha256(packed).hexdigest()}")

if __name__ == "__main__":
    main()
