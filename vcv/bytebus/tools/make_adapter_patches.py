"""Generate the two Adapter examples; --check compares without writing."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def patches():
    version = json.loads((ROOT / 'plugin.json').read_text())['version']
    cz_version = json.loads((ROOT.parent / 'czish/plugin.json').read_text())['version']

    def module(ident, plugin, model, pos, values=None, data=None):
        ver = version if plugin == 'ByteBusLab' else cz_version if plugin == 'CZishLab' else '2.6.4'
        result = dict(id=ident, plugin=plugin, model=model, version=ver, pos=pos,
                      params=[dict(id=int(k), value=float(v)) for k, v in (values or {}).items()])
        if data is not None:
            result['data'] = data
        return result

    def patch(modules, edges):
        return dict(version='2.6.4', modules=modules,
                    cables=[dict(id=i+1, outputModuleId=a, outputId=b, inputModuleId=c,
                                 inputId=d, color='#80ddc7') for i, (a, b, c, d) in enumerate(edges)])

    audio = patch([
        module(1, 'Fundamental', 'VCO', [0, 0]),
        # DAC Stage: provisional CZ word at 40 kHz. Native audio goes left.
        module(2, 'CZishLab', 'DACStage', [10, 0], {0: 1, 2: 40000}),
        # Adapter: CZ -> standard offset binary, numeric idle 80.
        module(3, 'ByteBusLab', 'Adapter', [28, 0], {0: 2, 6: 128}),
        module(4, 'ByteBusLab', 'Converter', [44, 0]),
        # Decoded bus -> Converter -> right. Select a real audio device after loading.
        module(5, 'Core', 'AudioInterface2', [58, 0]),
    ], [(1, 0, 2, 0), (2, 1, 3, 0), (3, 0, 4, 2), (2, 0, 5, 0), (4, 2, 5, 1)])
    triggers = patch([
        # Repeated 41 at 8 Hz: a lane hit plus Gorgon's FILL bit, held high.
        module(1, 'ByteBusLab', 'ClockedRegister', [0, 0], {0: 3, 1: 3, 2: 65}, {'word': 65}),
        module(2, 'ByteBusLab', 'Adapter', [12, 0], {4: 63}),
        module(3, 'Fundamental', 'Split', [28, 0]),
        module(4, 'Fundamental', 'Split', [32, 0]),
    ], [(1, 0, 2, 0), (2, 2, 3, 0), (2, 3, 4, 0)])
    return {'adapter-cz-audio.vcv': audio, 'adapter-repeated-triggers.vcv': triggers}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for name, value in patches().items():
        content = json.dumps(value, indent=2) + '\n'
        target = ROOT / 'examples' / name
        if args.check:
            if not target.exists() or target.read_text() != content:
                raise SystemExit(f'{target} differs; regenerate with {Path(__file__).name}')
        else:
            target.write_text(content)
    print('Adapter examples verified' if args.check else 'Wrote Adapter audio and repeated-trigger examples')


if __name__ == '__main__':
    main()
