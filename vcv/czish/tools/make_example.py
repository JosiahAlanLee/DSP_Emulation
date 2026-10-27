#!/usr/bin/env python3
"""Build the CZ encoded-word -> Register -> Yamaha DAC example patch."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def module(ident, plugin, model, version, pos, params):
    return dict(id=ident, plugin=plugin, model=model, version=version, pos=pos,
                params=[dict(id=i, value=float(v)) for i, v in enumerate(params)])


def patch():
    version = json.loads((ROOT / 'plugin.json').read_text())['version']
    # Register's port IDs have been stable since 2.3; do not make example
    # regeneration depend on unrelated ByteBusLab version bumps.
    byte_version = '2.3.0'
    modules = [
        # 2 Hz unipolar square gates the single-line CZ voice.
        module(1, 'Fundamental', 'LFO', '2.6.2', [0, 0], [1, 0, 1, 0, 0, .5, 0]),
        module(2, 'CZishLab', 'CZish', version, [9, 0], [0, 0, 64, 2, 200, 2, 0, 1]),
        # Register captures the CZ word at 1 Hz, independently of its bus CLK.
        module(3, 'ByteBusLab', 'ClockedRegister', byte_version, [25, 0], [3, 0, 0, 0]),
        # Low initial output level: native-word XOR can make sudden changes.
        module(4, 'CZishLab', 'DACStage', version, [37, 0], [0, 12, 55930.39, 0, 0, -12, 24000, 0, 1]),
        module(5, 'Core', 'AudioInterface2', '2.6.4', [53, 0], []),
    ]
    connections = [(1, 3, 2, 1), (2, 0, 4, 0), (2, 1, 3, 0),
                   (3, 0, 4, 3), (4, 0, 5, 0), (4, 0, 5, 1)]
    cables = [dict(id=i + 1, outputModuleId=src, outputId=out, inputModuleId=dst,
                   inputId=port, color='#80ddc7')
              for i, (src, out, dst, port) in enumerate(connections)]
    return dict(version='2.6.4', modules=modules, cables=cables)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    target = ROOT / 'examples/cz-yamaha-bytebus.vcv'
    text = json.dumps(patch(), indent=2) + '\n'
    if args.check:
        if target.read_text() != text:
            raise SystemExit('Example patch does not match its generator')
        print('Example patch matches its generator')
    else:
        target.parent.mkdir(exist_ok=True)
        target.write_text(text)
        print('Wrote CZ → Register → Yamaha DAC example')


if __name__ == '__main__':
    main()
