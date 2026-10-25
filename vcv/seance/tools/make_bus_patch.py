"""Generate the self-contained OTIS / spectral-byte feedback example."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def module(ident, plugin, model, version, pos, params, data=None):
    result = dict(id=ident, plugin=plugin, model=model, version=version, pos=pos,
                  params=[dict(id=i, value=v) for i, v in enumerate(params)])
    if data is not None:
        result['data'] = data
    return result


def patch():
    # Reuse our original, embedded stereo wavetable bank; no external sample paths.
    otis = json.loads((ROOT.parent / 'otislab/examples/OTIS-byte-frames.vcv').read_text())['modules'][2]
    modules = [
        {**otis, 'id': 1, 'pos': [0, 0]},
        module(2, 'SeanceLab', 'Seance', '2.1.0', [36, 0], [2, 0, 1, 0]),
        module(3, 'SeanceLab', 'SeanceBridge', '2.1.0', [46, 0], [-36, 6, 20]),
        module(4, 'ByteBusLab', 'ClockedRegister', '2.2.0', [54, 0], [3, 2, 165, 0], {'word': 165}),
        module(5, 'ByteBusLab', 'Lookup', '2.2.0', [66, 0], [4, 11, 0, 17, 0, 0, 0]),
        module(6, 'Core', 'AudioInterface2', '2.6.4', [82, 0], []),
    ]
    connections = [(1, 0, 2, 0), (3, 0, 4, 0), (4, 0, 5, 0),
                   (5, 0, 3, 0), (5, 0, 1, 16), (2, 0, 6, 0), (1, 1, 6, 1)]
    cables = [dict(id=i+1, outputModuleId=src, outputId=out,
                   inputModuleId=dest, inputId=port,
                   color='#80ddc7' if src in (3, 4, 5) else '#f0b46a')
              for i, (src, out, dest, port) in enumerate(connections)]
    return dict(version='2.6.4', modules=modules, cables=cables)


if __name__ == '__main__':
    path = ROOT / 'examples/OTIS-spectral-feedback.vcv'
    path.parent.mkdir(exist_ok=True)
    path.write_text(json.dumps(patch(), indent=2) + '\n')
    print(f'Created {path.name}')
