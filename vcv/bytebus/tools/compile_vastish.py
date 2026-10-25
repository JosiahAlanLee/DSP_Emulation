#!/usr/bin/env python3
"""Compile a plain JSON preset, Rack patch/archive, or frozen recipe to Lookup JSON."""
from __future__ import annotations
import argparse
import hashlib
import io
import json
import math
import os
from pathlib import Path
import subprocess
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MAX_BYTES = 64 * 1024 * 1024

def fnv(data):
    h = 14695981039346656037
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xffffffffffffffff
    return f"fnv1a64:{h:016x}"

def read_document(path):
    with path.open("rb") as stream:
        data = stream.read(MAX_BYTES+1)
    if len(data) > MAX_BYTES:
        raise ValueError("Input exceeds 64 MiB")
    if data.startswith(b"\x28\xb5\x2f\xfd"):
        with subprocess.Popen(["zstd","--decompress","--stdout",str(path)],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL) as process:
            data = process.stdout.read(MAX_BYTES+1)
            if len(data) > MAX_BYTES:
                process.kill()
                raise ValueError("Expanded patch exceeds 64 MiB")
            if process.wait() != 0:
                raise ValueError("Cannot decompress Rack patch")
    if data.startswith(b"PK"):
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            names = [n for n in archive.namelist() if n.endswith("patch.json")]
            if len(names) != 1:
                raise ValueError("Archive needs one patch.json")
            with archive.open(names[0]) as source:
                data = source.read(MAX_BYTES+1)
    elif not data.lstrip().startswith(b"{"):
        with tarfile.open(fileobj=io.BytesIO(data),mode="r:*") as archive:
            names = [n for n in archive.getmembers() if n.name.endswith("patch.json") and n.isfile()]
            if len(names) != 1:
                raise ValueError("Archive needs one patch.json")
            with archive.extractfile(names[0]) as source:
                data = source.read(MAX_BYTES+1)
    if len(data) > MAX_BYTES:
        raise ValueError("Patch JSON exceeds 64 MiB")
    return json.loads(data)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input",type=Path)
    parser.add_argument("--output","-o",type=Path,required=True)
    parser.add_argument("--module-id",type=int)
    parser.add_argument("--note",type=float)
    parser.add_argument("--velocity",type=float)
    parser.add_argument("--name")
    parser.add_argument("--backend",type=Path,default=ROOT/"tools"/("compile_vastish_backend.exe" if os.name == "nt" else "compile_vastish_backend"))
    args = parser.parse_args()
    try:
        describe = json.loads(subprocess.check_output([str(args.backend),"--describe"],text=True))
        if describe["parameter_count"] < 86 or describe["source_count"] < 23:
            raise ValueError("Backend predates Vastish 2.5; rebuild with: make -C vcv/bytebus/tools")
        source = read_document(args.input)
        if "modules" in source:
            candidates = [m for m in source["modules"] if m.get("model") == "Vastish" and m.get("plugin") == "VastishLab"]
            if args.module_id is not None:
                candidates = [m for m in candidates if m.get("id") == args.module_id]
            if len(candidates) != 1:
                raise ValueError("Select exactly one Vastish with --module-id")
            source = candidates[0]
        elif "model" in source and (source.get("model") != "Vastish" or source.get("plugin") != "VastishLab"):
            raise ValueError("Preset is not VastishLab/Vastish")
        elif "model" not in source and source.get("format") != "ByteBusLab.VastishRecipe/1":
            raise ValueError("Expected a Vastish preset/patch or ByteBusLab.VastishRecipe/1")
        count = describe["parameter_count"]
        params = list(describe["defaults"])
        supplied = source.get("params",[])
        if isinstance(supplied,list) and supplied and isinstance(supplied[0],dict):
            seen = set()
            for entry in supplied:
                key = entry["id"]
                if type(key) is not int or key < 0 or key >= count or key in seen:
                    raise ValueError("Invalid, duplicate or unsupported parameter id")
                params[key] = entry["value"]; seen.add(key)
        elif isinstance(supplied,list) and len(supplied) in (80,count):
            params[:len(supplied)] = supplied
        elif supplied or not isinstance(supplied,list):
            raise ValueError("params must be Rack id/value entries or a complete parameter array")
        frozen = source.get("frozen",{})
        note = args.note if args.note is not None else frozen.get("note",60)
        velocity = args.velocity if args.velocity is not None else frozen.get("velocity",1)
        sources = frozen.get("sources",[0]*describe["source_count"])
        if isinstance(sources,list) and len(sources)==18:
            sources = sources + [0]*(describe["source_count"]-18)
        values = [note,velocity,*params,*sources]
        if len(sources) != describe["source_count"] or any(type(v) not in (int,float) or not math.isfinite(v) for v in values):
            raise ValueError("Frozen values must be finite numbers with the declared source count")
        sources[22] = 1  # ONE is an unconditional native constant.
        values = [note,velocity,*params,*sources]
        request = " ".join(format(v,".9g") for v in values).encode()
        result = subprocess.run([str(args.backend)],input=request,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=False)
        if result.returncode:
            raise ValueError(result.stderr.decode(errors="replace").strip())
        if len(result.stdout) != 256:
            raise ValueError("Backend did not return exactly 256 bytes")
        table = result.stdout
        document = {
            "format":"ByteBusLab.LookupTable","version":1,"name":args.name or args.input.stem,
            "hash":fnv(table),"sha256":hashlib.sha256(table).hexdigest(),"values":list(table),
            "source":{"kind":"Vastish.memoryless","compiler":"compile_vastish v2","algorithm":"phase rotation then F1-F3, before amp/master/pan",
                      "params":params,"frozen":{"note":note,"velocity":velocity,"sources":sources}},
        }
        # Write only after a successful compile. A rejected patch leaves output untouched.
        args.output.write_text(json.dumps(document,indent=2,sort_keys=True)+"\n")
        print(f"Compiled {document['name']}: {document['hash']}")
    except (ValueError,OSError,KeyError,TypeError,subprocess.SubprocessError,tarfile.TarError,zipfile.BadZipFile) as error:
        help_text = "\nBuild the backend with: make -C vcv/bytebus/tools" if not args.backend.exists() else ""
        parser.exit(2,f"{error}{help_text}\n")

if __name__ == "__main__":
    main()
