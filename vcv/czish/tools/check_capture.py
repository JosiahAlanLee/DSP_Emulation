#!/usr/bin/env python3
"""Generate/check the deterministic owner-capture archive, not recordings."""
import argparse
import csv
import io
from pathlib import Path
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / 'tests/hardware/cz101-capture-fixtures.zip'


def events(data):
    assert data[:14] == bytes.fromhex('4d546864000000060000000103c0')
    assert data[14:18] == b'MTrk'
    assert int.from_bytes(data[18:22], 'big') == len(data) - 22
    offset, tick, active, durations = 22, 0, {}, []
    def vlq():
        nonlocal offset
        value = 0
        for _ in range(4):
            byte = data[offset]; offset += 1
            value = (value << 7) | (byte & 127)
            if byte < 128:
                return value
        raise AssertionError('MIDI variable length value exceeds four bytes')
    while offset < len(data):
        tick += vlq()
        status = data[offset]; offset += 1
        if status == 255:
            kind = data[offset]; offset += 1
            length = vlq()
            offset += length
            if kind == 47:
                assert not active and offset == len(data)
                return durations
        elif status in (128, 144):
            note, velocity = data[offset:offset + 2]; offset += 2
            assert note < 128 and velocity < 128
            if status == 144 and velocity:
                assert note not in active
                active[note] = tick
            else:
                assert note in active
                durations.append((note, tick - active.pop(note)))
        else:
            raise AssertionError(f'Unexpected MIDI status {status:x}')
    raise AssertionError('MIDI end event is missing')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        subprocess.run([str(ROOT / 'tests/render'), '--capture', str(directory)], check=True)
        manifest = (directory / 'manifest.tsv').read_text()
        rows = list(csv.DictReader(io.StringIO(manifest), delimiter='\t'))
        for row in rows:
            name = row['name']
            data = (directory / (name + '.syx')).read_bytes()
            assert len(data) == 263 and data[:6] == bytes.fromhex('f04400007030') and data[-1] == 247
            assert all(byte < 16 for byte in data[6:-1])
            durations = events((directory / (name + '.mid')).read_bytes())
            notes = [int(x) for x in row['notes'].split(',') if x]
            expected = round(float(row['held_seconds']) * 1920)
            assert len(durations) == len(notes) * (8 if name == 'c-bursts' else 1)
            assert all(note in notes and abs(ticks - expected) <= 1 for note, ticks in durations)
        if args.write:
            TARGET.parent.mkdir(exist_ok=True)
            with zipfile.ZipFile(TARGET, 'w', zipfile.ZIP_DEFLATED) as archive:
                for file in sorted(directory.iterdir()):
                    info = zipfile.ZipInfo(file.name, (1980, 1, 1, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    archive.writestr(info, file.read_bytes())
            (TARGET.parent / 'manifest.tsv').write_text(manifest)
            (ROOT / 'examples/original-detuned-chime.syx').write_bytes((directory / 'original-detuned-chime.syx').read_bytes())
        else:
            with zipfile.ZipFile(TARGET) as archive:
                assert set(archive.namelist()) == {p.name for p in directory.iterdir()}
                for file in directory.iterdir():
                    assert archive.read(file.name) == file.read_bytes(), file.name
            assert (TARGET.parent / 'manifest.tsv').read_text() == manifest
        print(f'{len(rows)} original capture tones and MIDI clips verified; archive is reproducible')


if __name__ == '__main__':
    main()
