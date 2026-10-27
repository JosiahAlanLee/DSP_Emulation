#!/usr/bin/env python3
"""Generate Lookup libraries v1 and v2 from the formulas below; no imported waveform/pattern data."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
N, SHAPES = 256, 64
FAMILIES = ["Harmonic", "Formant", "Fold", "Curve", "Walsh", "Rhythm"]
V2_FAMILIES = ["Envelope", "Sequence", "Density"]  # after Custom: FAMILY 7, 8 and 9

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

# Library v2: three families v1 lacks. v1 above is immutable; these are a new identity.

def level(v):
    return max(0, min(255, math.floor(v * 255 + .5)))

def fall(x, k):
    """A normalized exponential fall from 1 at x=0 to 0 at x=1; k=0 is linear."""
    if k == 0:
        return 1 - x
    return (math.exp(-k * x) - math.exp(-k)) / (1 - math.exp(-k))

def envelope(shape, u):
    group, k = divmod(shape, 8)
    t = k / 7
    if group == 0:   # exponential decays, linear to very sharp
        return fall(u, 9 * t)
    if group == 1:   # logarithmic decays: hold high, then fall
        return 1 - u ** (1.5 + 6.5 * t)
    if group == 2:   # attack then decay, attack 2% to 45%
        a = .02 + .43 * t
        return math.sin(math.pi / 2 * u / a) if u < a else fall((u - a) / (1 - a), 5)
    if group == 3:   # rises that hold at the top: expo through linear to log
        p = [4, 3, 2, 1.5][k] if k < 4 else [1.5, 2, 3, 4][k - 4]
        return u ** p if k < 4 else 1 - (1 - u) ** p
    if group == 4:   # plateaus and cliffs: full, held for 10%-80%, then a cliff
        h = .1 + .7 * t
        return 1. if u < h else max(0., 1 - (u - h) / .02)
    if group == 5:   # two-stage cliffs: full, a plateau at half, then zero
        h1 = .08 + .3 * t; h2 = h1 + .15 + .3 * t
        return 1. if u < h1 else (.5 if u < h2 else 0.)
    if group == 6:   # lumps: 2-5 swells, each lower than the last
        lumps = 2 + k // 2
        decay = .55 + .3 * (k % 2)
        x = u * lumps; i = min(int(x), lumps - 1); f = x - i
        return math.sin(math.pi * f) * decay ** i
    # group 7: attack, decay, then a held sustain of 0-88%
    s = .875 * t; a, d = .02, .3
    if u < a: return u / a
    if u < a + d: return s + (1 - s) * fall((u - a) / d, 4)
    return s

def euclid(hits, steps):
    return [(i * hits) % steps < hits for i in range(steps)]

def sequence(shape, n):
    group, k = divmod(shape, 16)
    if group == 0:   # staircases: up with 2-16 steps, then down with 16-2
        counts = [2, 3, 4, 5, 6, 8, 12, 16]
        L = counts[k] if k < 8 else counts[15 - k]
        i = n * L // N
        v = i / (L - 1)
        return v if k < 8 else 1 - v
    if group == 1:   # ridges: up and down over L steps, peak at the middle, then skewed
        L = [3, 4, 5, 6, 8, 10, 12, 16][k % 8]
        peak = .5 if k < 8 else [.25, .75][k % 2]
        i = n * L // N; x = i / (L - 1) if L > 1 else 0
        return x / peak if x <= peak else (1 - x) / (1 - peak)
    if group == 2:   # Euclidean ridges: steps that rise on E(h, 16); then up-and-down ridges on E(h, 16)
        h = 3 + k % 8
        hits = euclid(h, 16)
        i = n * 16 // N
        rank = sum(hits[:i + 1]) - 1
        if k < 8:
            return rank / (h - 1)
        return 1 - abs(2 * rank / (h - 1) - 1)
    # group 3: bounce: a ball dropped from the top, over 16 or 32 steps; each bounce keeps
    # r of the height (r = 0.45-0.80 by k), so flights shrink by sqrt(r)
    L = 16 if k < 8 else 32
    r = .45 + .05 * (k % 8)
    flights = [.5]                       # the drop is half a flight
    while len(flights) < 12:
        flights.append(math.sqrt(r) ** len(flights))
    x = (n * L // N + .5) / L * sum(flights)
    for j, d in enumerate(flights):
        if x <= d:
            f = x / d
            if j == 0:
                return 1 - f * f         # falling from the top
            return r ** j * 4 * f * (1 - f)
        x -= d
    return 0.

def density_order():
    """Ranked (lane, step) slots beyond the four anchors; each shape adds the next."""
    def metric(step):
        return 0 if step % 16 == 0 else 1 if step % 8 == 0 else 2 if step % 4 == 0 else 3 if step % 2 == 0 else 4
    affinity = [  # lane x metric level (bar, half bar, beat, 8th, 16th): lower comes earlier; None never
        [None, 5.0, 4.0, 6.0, 7.5],     # kick (never on the backbeat)
        [9.0, None, 6.5, 5.5, 6.8],     # snare: anchors on the backbeat, then ghosts
        [1.0, 1.0, 1.2, 3.0, 8.0],      # closed hat: beats, 8ths, and 16ths last
        [None, None, 9.0, 4.0, None],   # open hat: off-beat 8ths
        [None, 6.0, 5.0, 6.0, 7.0],     # percussion 1
        [None, 6.5, 5.5, 6.5, 7.5],     # percussion 2
        [None, 7.0, 6.0, 7.0, 8.0],     # percussion 3
        [None, 7.5, 6.3, 7.8, 7.2],     # percussion 4
    ]
    anchors = {(0, 0), (0, 16), (1, 8), (1, 24)}
    slots = []
    for lane in range(8):
        for step in range(32):
            if (lane, step) in anchors: continue
            a = affinity[lane][metric(step)]
            if a is None: continue
            if lane == 0 and step in (8, 24): continue        # kick off the backbeat
            if lane == 3 and step in (8, 24): continue
            jitter = (mix(lane * 32 + step + 0x44454e53) & 0xffff) / 65536
            slots.append((a + jitter, lane, step))
    slots.sort()
    return anchors, [(lane, step) for _, lane, step in slots]

def density(shape):
    anchors, order = density_order()
    hits = set(anchors) | set(order[:shape])
    return bytes((sum(1 << lane for lane, step in hits if step == n // 8) if n % 8 < 4 else 0) for n in range(N))

def build_v2():
    data = bytearray()
    records = []
    for family in V2_FAMILIES:
        for shape in range(SHAPES):
            if family == "Envelope":
                table = bytes(level(envelope(shape, n / 255)) for n in range(N))
            elif family == "Sequence":
                table = bytes(level(sequence(shape, n)) for n in range(N))
            else:
                table = density(shape)
            extra = {}
            if family == "Density":
                extra["hits"] = sum(bin(table[n]).count("1") for n in range(0, N, 8))
            records.append({"family":family,"shape":shape,"sha256":hashlib.sha256(table).hexdigest(),**extra})
            data.extend(table)
    header = b"BBLOOK02" + bytes([len(V2_FAMILIES), SHAPES, 0, 2])
    packed = header + data
    manifest = {
        "format":"ByteBusLab.LookupLibrary", "version":2, "generator":"generate_lookup.py v2",
        "families":V2_FAMILIES,"family_index_base":7,"shapes_per_family":SHAPES,"values_per_shape":N,
        "sha256":hashlib.sha256(packed).hexdigest(),"data_fnv1a64":f"{fnv(data):016x}",
        "density_order":[list(slot) for slot in density_order()[1][:SHAPES-1]],
        "provenance":"Original analytic envelopes, arithmetic step sequences, Euclidean distribution, a bounce model and a ranked metric order in generate_lookup.py; no external table data.",
        "tables":records,
    }
    return packed, json.dumps(manifest,indent=2,sort_keys=True)+"\n", fnv(data), manifest["sha256"]

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
    return packed, json.dumps(manifest,indent=2,sort_keys=True)+"\n", fnv(data), manifest["sha256"]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check",action="store_true",help="Regenerate and compare committed assets")
    args = parser.parse_args()
    packed, manifest, data_hash, sha = build()
    packed2, manifest2, data_hash2, sha2 = build_v2()
    identity = ("// Generated by tools/generate_lookup.py. Do not edit.\n#pragma once\n#include <cstdint>\n"
                "namespace dspemu::lookup {\n"
                f"constexpr uint64_t LIBRARY_HASH = 0x{data_hash:016x}ull;\n"
                f'constexpr const char* LIBRARY_SHA256 = "{sha}";\n'
                f"constexpr uint64_t LIBRARY_V2_HASH = 0x{data_hash2:016x}ull;\n"
                f'constexpr const char* LIBRARY_V2_SHA256 = "{sha2}";\n'
                "} // namespace dspemu::lookup\n")
    files = {ROOT/"res/lookup-v1.bin":packed,ROOT/"res/lookup-v1.json":manifest.encode(),
             ROOT/"res/lookup-v2.bin":packed2,ROOT/"res/lookup-v2.json":manifest2.encode(),
             ROOT/"src/engine/LibraryIdentity.hpp":identity.encode()}
    for path, content in files.items():
        if args.check:
            if not path.exists() or path.read_bytes() != content:
                raise SystemExit(f"Generated asset differs: {path}")
        else:
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(content)
    print(f"PASS: original Lookup v1, 6 families x 64 shapes x 256 bytes, SHA256 {sha}")
    print(f"PASS: original Lookup v2, 3 families x 64 shapes x 256 bytes, SHA256 {sha2}")

if __name__ == "__main__":
    main()
