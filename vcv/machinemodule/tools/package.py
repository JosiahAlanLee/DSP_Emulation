"""Create Rack's tar+zstd plugin package with matching source alongside it."""
from pathlib import Path
import json
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
m = json.loads((ROOT/'plugin.json').read_text())
stage = ROOT/'dist'/m['slug']
if stage.exists():
    shutil.rmtree(stage)
stage.mkdir(parents=True)
for item in ['plugin.json', 'LICENSE', 'README.md', 'res', 'docs']:
    src = ROOT/item
    if src.is_dir(): shutil.copytree(src, stage/item)
    else: shutil.copy2(src, stage/item)
binary = next((ROOT/name for name in ['plugin.so', 'plugin.dylib', 'plugin.dll'] if (ROOT/name).is_file()), None)
if binary is None: raise SystemExit('Build the plugin first')
shutil.copy2(binary, stage/binary.name)
arch = 'lin-x64' if binary.suffix == '.so' else 'mac' if binary.suffix == '.dylib' else 'win-x64'
tar = ROOT/'dist'/f"{m['slug']}-{m['version']}-{arch}.tar"
with tarfile.open(tar, 'w') as archive:
    archive.add(stage, arcname=m['slug'])
out = tar.with_suffix('.vcvplugin')
subprocess.run(['zstd', '-f', '-q', str(tar), '-o', str(out)], check=True)
tar.unlink()
print(out)
