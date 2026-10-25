"""Generate an original stereo frame bank and a self-contained byte-bus patch."""
import base64
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def pcm(side):
    values = []
    for frame in range(8):
        for i in range(256):
            phase = 2 * math.pi * i / 256
            value = sum(math.sin(phase * harmonic + side * .17 * (harmonic - 1))
                        * math.exp(-abs(harmonic - (1 + frame * .8)) * .55) / harmonic
                        for harmonic in range(1, 13))
            values.append(max(-32768, min(32767, round(value * 22000))))
    return base64.b64encode(struct.pack('<' + 'h' * len(values), *values)).decode('ascii')


def module(ident, plugin, model, version, pos, params, data=None):
    result = dict(id=ident, plugin=plugin, model=model, version=version, pos=pos,
                  params=[dict(id=i, value=v) for i, v in enumerate(params)])
    if data is not None:
        result['data'] = data
    return result


def patch():
    # Existing IDs stay fixed; FRAME BUS is OTIS input 16.
    modules = [
        module(1, 'ByteBusLab', 'ClockedRegister', '2.1.0', [0, 0], [3, 1, 165, 0], {'word': 165}),
        module(2, 'ByteBusLab', 'Lookup', '2.1.0', [12, 0], [3, 32, 0, 0, 0, 0, 0]),
        module(3, 'OtisLab', 'OtisSampler', '2.8.0', [28, 0],
               [0, 0, 1, 1, 1, 1, .8, 5, 0, 0, 1, 0, 0, 9, 0, 8, 2, .5, .5, 0],
               dict(schema=1, rate=10000000/336, name='Original eight-frame stereo bank',
                    pcm16le=pcm(0), pcm16le_r=pcm(1), engineMode=0, filterMode=2,
                    smoothFrameBus=True)),
        module(4, 'Core', 'AudioInterface2', '2.6.4', [64, 0], []),
    ]
    cables = []
    for src, out, dest, port in [(1, 0, 2, 0), (2, 0, 1, 0), (1, 0, 3, 16), (3, 0, 4, 0), (3, 1, 4, 1)]:
        cables.append(dict(id=len(cables)+1, outputModuleId=src, outputId=out,
                           inputModuleId=dest, inputId=port, color='#80ddc7'))
    return dict(version='2.6.4', modules=modules, cables=cables)


if __name__ == '__main__':
    path = ROOT / 'examples/OTIS-byte-frames.vcv'
    path.write_text(json.dumps(patch(), indent=2) + '\n')
    print(f'Created {path.name}')
