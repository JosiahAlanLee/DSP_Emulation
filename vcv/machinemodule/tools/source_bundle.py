"""Bundle corresponding adapter/core source, never user OS/sample data."""
from pathlib import Path
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
CORE = Path(sys.argv[1]).resolve()
COMMIT = 'a2b6cf2478cbe94dfe30bc42da39e221df0e2556'
assert subprocess.check_output(['git', '-C', str(CORE), 'rev-parse', 'HEAD'], text=True).strip() == COMMIT
out = ROOT/'dist'; out.mkdir(exist_ok=True)
name = out/'MachinemoduleRack-2.0.0-source.tar.gz'


def add_tree(tar, source, target):
    for path in sorted(source.rglob('*')):
        relative = path.relative_to(source)
        if any(part in ('.git', '_deps', 'dist', '__pycache__', 'build') or part.startswith('build-') for part in relative.parts):
            continue
        if not path.is_file() or path.suffix.lower() in ('.syx', '.bin', '.so', '.dll', '.dylib', '.vcvplugin'):
            continue
        tar.add(path, arcname=str(target/relative), recursive=False)


with tarfile.open(name, 'w:gz') as tar:
    add_tree(tar, ROOT, Path('MachinemoduleRack/vcv/machinemodule'))
    tar.add(ROOT.parent/'shared/GorgonSound.hpp', arcname='MachinemoduleRack/vcv/shared/GorgonSound.hpp')
    # Panel sources use the same palette, outline writer and twelve marks as the
    # other modules. Include them so the source archive can regenerate artwork.
    for part in ['panels', 'glyphs']:
        add_tree(tar, ROOT.parent/'shared'/part, Path('MachinemoduleRack/vcv/shared')/part)
    # Only the core build's source dependencies; omit JUCE/MPC wrapper/graphics.
    for part in ['CMakeLists.txt', 'LICENSE', 'README.md', 'cmake', 'engine', 'tools', 'desktop/src/core',
                 'libs/dsp56300/LICENSE.md', 'libs/dsp56300/source',
                 'libs/gearmulator-md-mm/LICENSE.md', 'libs/gearmulator-md-mm/source/mc68k']:
        path = CORE/part
        target = Path('MachinemoduleRack/vcv/machinemodule/_deps/machinemodule')/part
        if path.is_dir():
            add_tree(tar, path, target)
        else:
            tar.add(path, arcname=str(target))
    # The archive does not need git metadata: CMake accepts this revision manifest.
    data = COMMIT+'\n'
    import io
    info = tarfile.TarInfo('MachinemoduleRack/vcv/machinemodule/_deps/machinemodule/MACHINEMODULE_REVISION')
    info.size = len(data); tar.addfile(info, io.BytesIO(data.encode()))
print(name)
