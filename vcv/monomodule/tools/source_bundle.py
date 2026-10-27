"""Bundle the corresponding source of the plugin binary, never the user's OS file.

    python3 tools/source_bundle.py CORE_DIR DSP56300_DIR
"""
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
CORE, DSP = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
COMMIT = '23e541021b92d1a5978f36c0a552ac443376df1a'
DSP_COMMIT = 'c051afad31612c2d2c7a81a7ab23e1c5ac9e61af'
assert subprocess.check_output(['git', '-C', str(CORE), 'rev-parse', 'HEAD'], text=True).strip() == COMMIT
assert subprocess.check_output(['git', '-C', str(DSP), 'rev-parse', 'HEAD'], text=True).strip() == DSP_COMMIT
out = ROOT/'dist'; out.mkdir(exist_ok=True)
version = json.loads((ROOT/'plugin.json').read_text())['version']
name = out/f'MonomoduleRack-{version}-source.tar.gz'
TOP = Path('MonomoduleRack/vcv')


def add_tree(tar, source, target):
    for path in sorted(source.rglob('*')):
        relative = path.relative_to(source)
        if any(part in ('.git', '_deps', 'dist', '__pycache__', 'build') or part.startswith('build-') for part in relative.parts):
            continue
        if not path.is_file() or path.suffix.lower() in ('.syx', '.bin', '.so', '.dll', '.dylib', '.vcvplugin', '.log'):
            continue
        tar.add(path, arcname=str(target/relative), recursive=False)


def add(tar, base, parts, target):
    for part in parts:
        path = base/part
        if path.is_dir():
            add_tree(tar, path, target/part)
        else:
            tar.add(path, arcname=str(target/part))


def manifest(tar, path, text):
    info = tarfile.TarInfo(str(path)); info.size = len(text)
    tar.addfile(info, io.BytesIO(text.encode()))


with tarfile.open(name, 'w:gz') as tar:
    add_tree(tar, ROOT, TOP/'monomodule')
    # The shared adapter this plugin is built from (AGPL-3.0-only, vcv/adapter/README.md).
    add_tree(tar, ROOT.parent/'adapter', TOP/'adapter')
    tar.add(ROOT.parent/'shared'/'GorgonSound.hpp', arcname=str(TOP/'shared'/'GorgonSound.hpp'))
    for part in ['panels', 'glyphs']:
        add_tree(tar, ROOT.parent/'shared'/part, TOP/'shared'/part)
    # Monomodule's engine and the emulator as the binary compiles them: dsp56300 with Monomodule's patch applied.
    add(tar, CORE, ['LICENSE', 'README.md', 'THIRD_PARTY_NOTICES.md', 'cmake', 'ext', 'src/core'], TOP/'monomodule/_deps/monomodule')
    add(tar, DSP, ['LICENSE.md', 'source/asmjit', 'source/dsp56kBase', 'source/dsp56kEmu', 'source/vtuneSdk'],
        TOP/'monomodule/_deps/dsp56300')
    # Without git metadata, CMake reads this revision manifest instead.
    manifest(tar, TOP/'monomodule/_deps/monomodule/MONOMODULE_REVISION', COMMIT+'\n')
    manifest(tar, TOP/'monomodule/_deps/dsp56300/DSP56300_REVISION', DSP_COMMIT+' with ext/patches/0001-dsp56300-mnm.patch\n')
print(name)
