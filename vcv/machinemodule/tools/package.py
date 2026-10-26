"""Create Rack's tar+zstd plugin package with matching source alongside it."""
from pathlib import Path
import argparse
import json
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, help='Select a binary when multiple platforms were built in this checkout')
args = parser.parse_args()
binaries = [ROOT/name for name in ['plugin.so', 'plugin.dylib', 'plugin.dll'] if (ROOT/name).is_file()]
if args.binary:
    binary = args.binary.resolve()
elif len(binaries) == 1:
    binary = binaries[0]
else:
    raise SystemExit('Build the plugin first, or use --binary to select among multiple builds')

# Read the target, not the packaging machine: Intel builds can run on ARM Macs.
with binary.open('rb') as stream:
    header = stream.read(64)
    if header[:4] == b'\x7fELF' and header[4:6] == b'\x02\x01' and int.from_bytes(header[18:20], 'little') == 62:
        arch = 'lin-x64'
    elif header[:4] == b'\xcf\xfa\xed\xfe':
        cpu = int.from_bytes(header[4:8], 'little')
        arch = {0x01000007: 'mac-x64', 0x0100000c: 'mac-arm64'}.get(cpu)
    elif header[:2] == b'MZ' and len(header) == 64:
        stream.seek(int.from_bytes(header[60:64], 'little'))
        pe = stream.read(6)
        arch = 'win-x64' if pe == b'PE\0\0\x64\x86' else None
    else:
        arch = None
if not arch:
    raise SystemExit(f'Unsupported or invalid plugin binary: {binary}')
expected_name = 'plugin.dylib' if arch.startswith('mac-') else 'plugin.dll' if arch.startswith('win-') else 'plugin.so'
if binary.name != expected_name:
    raise SystemExit(f'{arch} binary must be named {expected_name}')
m = json.loads((ROOT/'plugin.json').read_text())
stage = ROOT/'dist'/m['slug']
if stage.exists():
    shutil.rmtree(stage)
stage.mkdir(parents=True)
for item in ['plugin.json', 'LICENSE', 'README.md', 'res', 'docs']:
    src = ROOT/item
    if src.is_dir(): shutil.copytree(src, stage/item)
    else: shutil.copy2(src, stage/item)
shutil.copy2(binary, stage/binary.name)
if arch.startswith('mac-'):
    # Match Rack's plugin.mk: fix the SDK install name, then sign the final bytes.
    staged_binary = stage/binary.name
    subprocess.run(['install_name_tool', '-change', 'libRack.dylib', '/tmp/Rack2/libRack.dylib', str(staged_binary)], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(staged_binary)], check=True)
    subprocess.run(['codesign', '--verify', '--strict', str(staged_binary)], check=True)
tar = ROOT/'dist'/f"{m['slug']}-{m['version']}-{arch}.tar"
with tarfile.open(tar, 'w') as archive:
    archive.add(stage, arcname=m['slug'])
out = tar.with_suffix('.vcvplugin')
subprocess.run(['zstd', '-f', '-q', str(tar), '-o', str(out)], check=True)
tar.unlink()
print(out)
